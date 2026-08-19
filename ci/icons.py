#!/usr/bin/env python3
# ======================================================================
# ci/icons.py - every internal verb resolves to a real icon
# ----------------------------------------------------------------------
# ui_icon_for_command picks a desktop/drawer/group icon by running the
# command through ~47 SUBSTRING tests in order.  That is fine until a
# verb is added whose spelling none of them match, and then the applet
# quietly gets the generic file page - which nobody notices, because
# nobody opens every applet from a desktop icon.
#
# Four had drifted that way: "picshow" (while "pictures" and "gallery"
# both matched), "effects", "tunes", and - worse than a missing icon - a
# WRONG one, since "lightshow" contains "lights" and the Light Show was
# being drawn with the Lights Out lamp grid.
#
# This re-implements the matcher by parsing the chain out of ui.c in
# order, rather than hard-coding a copy of it that would drift in its
# own right.  If the parse finds no rules it FAILS: an empty rule list
# would otherwise report every verb as broken, or - worse, depending on
# how it is read - nothing at all.
# ======================================================================
import re
import sys

# Verbs that are meant to have no icon of their own.
EXEMPT = {
    'bsod', 'crash',      # the blue-screen easter egg, not a listed applet
    'recentclear',        # what Start > Documents > Clear the list calls
}


def rules_from(path):
    src = open(path).read()
    try:
        body = src[src.index('static int icon_for_folded'):]
        body = body[:body.index('return ICON_FILE;')]
    except ValueError:
        # Renamed or restructured: say so in one line rather than in a
        # traceback, and let the rule-count guard below fail the run.
        print("  ! could not find icon_for_folded's chain in %s" % path)
        return []
    out = []
    for m in re.finditer(r'if \(((?:[^;{}]|\n)*?)\)\s*\n?\s*return (ICON_\w+);',
                         body):
        cond, icon = m.group(1), m.group(2)
        subs = re.findall(r'has\("([^"]+)"\)', cond)
        nots = re.findall(r'!has\("([^"]+)"\)', cond)
        exact = re.findall(r'strcmp\(g_lc, "([^"]+)"\) == 0', cond)
        out.append((subs, nots, exact, icon))
    return out


def verbs_from(path):
    m = re.search(r'INTERNAL_VERBS\[\] = \{(.*?)\};', open(path).read(), re.S)
    return sorted(set(re.findall(r'"([a-z0-9]+)"', m.group(1))))


def icon_for(rules, cmd):
    for subs, nots, exact, icon in rules:
        if any(n in cmd for n in nots):
            continue
        if any(s in cmd for s in subs) or cmd in exact:
            return icon
    return 'ICON_FILE'


def main():
    rules = rules_from('src/ui.c')
    verbs = verbs_from('src/main.c')
    print("    %d icon rules, %d verbs" % (len(rules), len(verbs)))
    if len(rules) < 40 or len(verbs) < 40:
        print("  ! parsed %d rules and %d verbs out of the sources - this "
              "check is broken, not the code" % (len(rules), len(verbs)))
        return 1
    bad = 0
    for v in verbs:
        if v in EXEMPT:
            continue
        if icon_for(rules, v) == 'ICON_FILE':
            print("  ! the verb '%s' has no icon - ui_icon_for_command "
                  "falls through to the generic file page" % v)
            bad = 1
    return bad


sys.exit(main())
