#!/usr/bin/env python3
# ======================================================================
# ci/assoc.py - a picker must offer what its applet is registered for
# ----------------------------------------------------------------------
# main.c's assoc_app_for() decides which applet opens a double-clicked
# file.  Each of those applets also has a Load/Open box, and the filter
# it passes to filedlg() is a SEPARATE list written by hand somewhere
# else - so the two drift, and when they do the applet opens a file it
# then refuses to show you.  Two had:
#
#   the Gramophone   plays wav and mid; the "play" verb's picker asked
#                    for *.WA? and could not reach a .MID at all, while
#                    its own Eject button asked for *.* and offered
#                    CASTALIA.EXE
#   the Scrap Box    is registered for txt, doc, me, log, ini and asc,
#                    and its Load box offered *.TXT
#
# So: for every extension routed to an applet, that applet's picker
# pattern must match it.  The patterns are DOS 8.3 wildcards separated
# by semicolons, and matching one here is a two-character job - '?' is
# any character and the extensions are at most three long.
#
# A picker of "*.*" is accepted as covering everything; the Hex Peek and
# Copy/Move use it on purpose.
# ======================================================================
import re
import sys

# Where each applet's Load/Open filter lives.  Save-side filters are
# deliberately narrower (naming a NEW file) and are not checked.
PICKERS = {
    'scrap':  ('src/scrap.c',  'Open a document'),
    'paint':  ('src/paint.c',  'Open a drawing'),
    'gram':   ('src/main.c',   'Play a sound'),
}
# Routed to DOS or to a full-screen player with no picker of its own.
SKIP = {'dos', 'cinema'}


def routes(path):
    src = open(path).read()
    try:
        body = src[src.index('static const char *assoc_app_for'):]
        body = body[:body.index('return "peek";')]
    except ValueError:
        print("  ! could not find assoc_app_for in %s" % path)
        return {}
    out = {}
    for cond, app in re.findall(
            r'((?:has_ext\(name, "\w+"\)\s*\|\|?\s*)*has_ext\(name, "\w+"\))'
            r'\s*\)?\s*return\s+"(\w+)";', body):
        out.setdefault(app, set()).update(
            re.findall(r'has_ext\(name, "(\w+)"\)', cond))
    return out


def patterns_in(path, title):
    """The filedlg pattern passed alongside this dialog title."""
    src = open(path).read()
    m = re.search(r'filedlg\(\s*"' + re.escape(title) +
                  r'"\s*,\s*"([^"]+)"', src)
    return m.group(1) if m else None


def matches(pat, ext):
    """One DOS 8.3 wildcard against a bare extension."""
    if pat == '*.*':
        return True
    if not pat.startswith('*.'):
        return False
    tail = pat[2:]
    if tail == '*':
        return True
    if len(ext) > len(tail):
        return False                      # "midi" cannot match "MI?"
    for i, c in enumerate(tail):
        if c == '?':
            continue
        if i >= len(ext) or c.upper() != ext[i].upper():
            return False
    return True


def main():
    routed = routes('src/main.c')
    if len(routed) < 4:
        print("  ! parsed only %d applets out of assoc_app_for - this "
              "check is broken, not the code" % len(routed))
        return 1
    bad = 0
    for app, exts in sorted(routed.items()):
        if app in SKIP:
            continue
        if app not in PICKERS:
            print("  ! %s opens %s and ci/assoc.py does not know where its "
                  "picker is" % (app, ','.join(sorted(exts))))
            bad = 1
            continue
        path, title = PICKERS[app]
        pat = patterns_in(path, title)
        if pat is None:
            print("  ! no filedlg(\"%s\", ...) found in %s" % (title, path))
            bad = 1
            continue
        for ext in sorted(exts):
            if len(ext) > 3:
                continue                  # 8.3 cannot hold it anyway
            if not any(matches(p, ext) for p in pat.split(';')):
                print("  ! .%s opens in '%s', whose picker filter \"%s\" "
                      "does not list it" % (ext.upper(), app, pat))
                bad = 1
    print("    %d applets routed, %d with pickers checked"
          % (len(routed), len([a for a in routed if a not in SKIP])))
    return bad


sys.exit(main())
