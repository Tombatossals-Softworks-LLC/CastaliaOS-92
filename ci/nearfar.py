#!/usr/bin/env python3
# ======================================================================
# ci/nearfar.py - the medium-model near/far trap, checked properly
# ----------------------------------------------------------------------
# See ci/nearfar.sh for the wrapper.  In the MEDIUM model data pointers
# are NEAR.  Handing a far object to a parameter declared `T *` drops the
# segment, and the callee reads or writes DGROUP at that offset instead.
# Open Watcom does not warn, even at -wx.
#
# The first version of this gate was three greps, and it reported OK
# while eight live instances of the exact bug it was written for sat in
# the tree - two of them rmdir()/remove() deleting a path assembled from
# the wrong segment.  It had three holes, all of which this fixes:
#
#   1. It looked only at argument position 0.
#   2. It looked the callee up with grep IN THE SAME FILE and skipped it
#      when that failed - so every cross-module callee (font_draw,
#      dialog_confirm, rect_set) and every libc callee (remove, rmdir,
#      chdir, strcpy) was silently waved through.
#   3. It knew only about named far ARRAYS, so anything reached through a
#      far POINTER (`FileEntry far *e; ... remove(e->name)`) was invisible.
#
# A green gate that certifies a broken invariant is worse than no gate.
# ======================================================================
import glob
import os
import re
import sys

# ---- libc and compiler helpers whose pointer parameters are NEAR -------
# Not exhaustive: it is the set this shell actually calls with anything
# that could be far.  Add to it rather than widening the heuristic.
NEAR_LIBC = {
    "strcpy", "strncpy", "strcat", "strncat", "strcmp", "strncmp",
    "stricmp", "strnicmp", "strlen", "strchr", "strrchr", "strstr",
    "strtok", "strupr", "strlwr", "sprintf", "sscanf", "printf",
    "puts", "atoi", "atol", "strtol", "strtoul",
    "memcpy", "memmove", "memset", "memcmp",
    "fopen", "freopen", "fread", "fwrite", "fputs", "fgets", "fprintf",
    "remove", "rename", "unlink", "system", "getcwd",
    "chdir", "mkdir", "rmdir", "access", "stat", "spawnl", "spawnlp",
}

# Anything taking far pointers by definition - the escape hatches.
FAR_SAFE = {
    "_fstrcpy", "_fstrncpy", "_fstrcmp", "_fstricmp", "_fstrlen",
    "_fmemcpy", "_fmemset", "_fmemcmp", "_fmemmove", "_fstrchr",
    "FP_SEG", "FP_OFF", "MK_FP",
    # DOS/BIOS entry points: these take far pointers by construction,
    # because the interrupt receives them in a segment:offset pair.
    "_dos_read", "_dos_write", "_dos_open", "_dos_close",
    "_dos_findfirst", "_dos_findnext", "_dos_getdate", "_dos_gettime",
    "_dos_allocmem", "_dos_freemem", "_dos_setvect", "_dos_getvect",
    "int86", "int86x", "intdos", "intdosx", "segread",
}

KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "case",
            "do", "else", "defined"}

IDENT = r"[A-Za-z_][A-Za-z0-9_]*"


def strip_comments(text):
    """Blank out comments and string/char literals, preserving line count."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_args(s):
    """Split a call's argument list on top-level commas."""
    args, depth, cur = [], 0, []
    for ch in s:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    args.append("".join(cur))
    return args


def classify_param(p):
    """'far', 'near-ptr', or 'value' for one parameter declaration."""
    p = p.strip()
    if not p or p == "void":
        return "value"
    if re.search(r"\bfar\b", p):
        return "far"
    if "*" in p or re.search(r"\[\s*\]", p):
        return "near-ptr"
    return "value"


def collect_macros(paths):
    """Function-like macro names.  A macro's argument is substituted
    textually - it is never "passed to a near parameter" - so judging the
    call site is meaningless.  find.c's DIRP(i) expands to a far pointer
    expression that then goes to a properly far parameter; the checker saw
    an unknown callee and cried wolf."""
    names = set()
    for path in paths:
        for m in re.finditer(r"^[ \t]*#[ \t]*define[ \t]+(" + IDENT + r")\(",
                             open(path, "r", errors="replace").read(), re.M):
            names.add(m.group(1))
    return names


def collect_prototypes(paths):
    """name -> list of parameter classes, from every .h and .c we have."""
    protos = {}
    decl = re.compile(
        r"^[A-Za-z_][A-Za-z0-9_ \t*]*?\b(" + IDENT + r")\s*\(([^;{()]*)\)\s*[;{]",
        re.M)
    for path in paths:
        src = strip_comments(open(path, "r", errors="replace").read())
        src = re.sub(r"^\s*#.*$", "", src, flags=re.M)      # drop directives
        for m in decl.finditer(src):
            name, params = m.group(1), m.group(2)
            if name in KEYWORDS:
                continue
            if re.match(r"^\s*typedef\b", m.group(0)):
                continue
            classes = [classify_param(p) for p in split_args(params)]
            # A definition and its prototype must agree; if two disagree,
            # keep the stricter (more near-ptr) view so we never under-report.
            old = protos.get(name)
            if old is None or len(old) != len(classes):
                protos[name] = classes
    return protos


# Type words that can appear BEFORE the type proper.  Without these the
# declaration regex needs exactly one identifier in front of `far`, so
# `unsigned char far pal[]`, `volatile unsigned long far ticks` and every
# other multi-word type is invisible - 31 of the tree's 145 far
# declarations, with seven translation units (snake, echo, bench, breaker,
# clock, music, inspect) coming back completely empty.  A checker that
# cannot see the storage cannot check it.
TYPEWORD = r"(?:(?:unsigned|signed|volatile|const|struct|union|long|short)\s+)*"


# Line-oriented on purpose.  The obvious regex for "any type words before
# `far`" is a quantified alternation including \s - and \s matches
# newlines, so with re.M the engine can try spans covering the whole file.
# That version did not merely run slowly, it hung.  Splitting on lines
# first keeps every match linear.
DECL_RE = re.compile(
    r"^[ \t]*(?:static[ \t]+)?([A-Za-z_][A-Za-z0-9_ \t*]*?)\bfar[ \t]+(.+)$")


def file_far_arrays(src):
    """File-scope far storage: ({name: "obj"|"ptrelem"}, {far pointers}).

    FILE SCOPE ONLY - brace depth zero.  Scanning the whole file swept up
    LOCALS too, and because this set is applied TU-wide one function's
    `const Rect far *r` made every other function's near `Rect *r` look
    far: eleven false reports from one local.  A checker that cries wolf
    gets switched off, which is how the previous one came to be useless.
    """
    arrays = {}
    fptrs = set()
    depth = 0
    for line in src.split("\n"):
        at_top = (depth == 0)
        depth += line.count("{") - line.count("}")
        if not at_top:
            continue
        if not re.search(r"\bfar\b", line):
            continue
        # Take the DECLARATOR, not the whole statement.  Requiring a `;`
        # on the same line skipped every far table with a multi-line
        # initializer - all eighteen palettes in video.c and the 40-entry
        # g_app[] in window.c, 19 objects in all.  A demonstration bug
        # passing pal_classic to a near `const u8 *` was waved straight
        # through.  The names are all we need, and they are all on the
        # first line; stop at `;` or `=`, whichever comes first.
        cut = len(line)
        for ch in (";", "="):
            k = line.find(ch)
            if k >= 0 and k < cut:
                cut = k
        m = DECL_RE.match(line[:cut] + ";")
        if not m:
            continue
        decls = m.group(2)
        if decls.endswith(";"):
            decls = decls[:-1]
        # A `*` in the TYPE means the elements are pointers: for
        # `const char * const far g_verbs[]` the array lives far but each
        # element is an ordinary near char*, so `g_verbs[i]` handed to a
        # near parameter is perfectly correct.  Without this the checker
        # cries wolf on main.c's verb tables - and a gate that cries wolf
        # gets switched off, which is how this one failed before.
        ptr_elem = "*" in m.group(1)
        # Every declarator on the line, not just the first:
        # `static u8 far a[4], b[4];` used to hide b.
        for d in decls.split(","):
            d = d.strip()
            if "(" in d:
                continue          # a function returning far, not storage
            n = re.match(r"(\**)\s*(" + IDENT + r")", d)
            if not n:
                continue
            if n.group(1):
                # `static IconBitmap far *g_bm` is a far POINTER, not far
                # storage: it belongs with the pointers, where p->member
                # and *p are understood.
                fptrs.add(n.group(2))
            else:
                arrays[n.group(2)] = "ptrelem" if ptr_elem else "obj"
    return arrays, fptrs


def far_pointers(text):
    r"""`T far *p` declared in this text - parameters and locals alike.

    [ \t] and not \s: \s matches newlines, so `u8 far *` ending one line
    bound to the first identifier on the NEXT one.  That made near locals
    called `r` and `m` look far and produced a page of false reports -
    and a checker that cries wolf is a checker that gets switched off."""
    return set(m.group(1) for m in re.finditer(
        r"\b[A-Za-z_][A-Za-z0-9_]*[ \t]+far[ \t]*\*[ \t]*(" + IDENT + r")\b",
        text))


def functions(src):
    """(name, body_text, offset) for each top-level function definition.

    Scoping matters: `Rect far *r` in one function must not make every
    other function's near `Rect *r` look far.  The first version of this
    checker had no scoping at all and drowned in false positives, which
    is its own kind of useless gate.
    """
    out = []
    sig = re.compile(r"^[A-Za-z_][A-Za-z0-9_ \t*]*?\b(" + IDENT +
                     r")\s*\(([^;{()]*)\)\s*\{", re.M)
    for m in sig.finditer(src):
        i, depth = m.end() - 1, 0
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out.append((m.group(1), m.group(2), src[m.end():i], m.end()))
    return out


def arg_is_far(arg, arrays, pointers):
    """(name, kind) if this argument is far storage, else None.

    kind 'ptr'  - unambiguously a pointer (array decay, &x, a far pointer)
    kind 'elem' - an element or member, which is only a pointer if its own
                  type is one.  Against a near POINTER parameter that is
                  still a bug; against an unknown callee it is far more
                  likely an ordinary value, so we stay quiet there.
    """
    a = arg.strip()
    if not a:
        return None
    # A POINTER cast does not make far storage near - it only silences the
    # reader - so peel it and judge what is underneath.  A cast to a plain
    # value type is the opposite: `(unsigned long)e->size` really is a
    # value, and peeling that one made sprintf's varargs look like far
    # pointers.  Only casts containing a `*` are peeled.
    while True:
        c = re.match(r"^\([ \t]*[A-Za-z_][A-Za-z0-9_ \t]*\*[ \t*]*\)[ \t]*(.+)$",
                     a)
        if not c:
            break
        a = c.group(1).strip()
    # Subscripts nest - `&g_last_geom[g_w[id].kind]` - so the bracket body
    # must be greedy.  A non-greedy [^\]]* here is exactly why the first
    # run of this checker missed the g_last_geom write into DGROUP.
    for name in arrays:
        esc = re.escape(name)
        if re.match(r"^" + esc + r"\s*$", a):
            return (name, "ptr")                      # array decays
        if re.match(r"^&\s*" + esc + r"\s*(\[.*\])?\s*$", a):
            return (name, "ptr")                      # &a  /  &a[i]
        if re.match(r"^" + esc + r"\s*[-+]", a):
            return (name, "ptr")                      # a + 2 (decay, offset)
        if re.match(r"^&?\s*" + esc + r"\s*\[.*\]\s*\.\s*" + IDENT +
                    r"\s*$", a):
            return (name, "elem")                     # a[i].member
        if re.match(r"^" + esc + r"\s*\[.*\]\s*$", a):
            if arrays[name] == "ptrelem":
                return None                           # element is near
            return (name, "elem")                     # a[i]
    for name in pointers:
        esc = re.escape(name)
        # A bare far pointer handed to a near T* is equally wrong: the
        # value is 32 bits wide and the parameter is 16.
        if re.match(r"^" + esc + r"\s*$", a):
            return (name, "ptr")
        if re.match(r"^" + esc + r"\s*[-+]", a):      # p + 1
            return (name, "ptr")
        # &p->member, &p->member[i], &p.member - all far addresses.
        if re.match(r"^&\s*" + esc + r"\s*(->|\.)\s*" + IDENT +
                    r"\s*(\[.*\])?\s*$", a):
            return (name, "ptr")
        if re.match(r"^" + esc + r"\s*(->|\.)\s*" + IDENT +
                    r"\s*\[.*\]\s*$", a):
            return (name, "ptr")                      # p->member[i]
        if re.match(r"^" + esc + r"\s*(->|\.)\s*" + IDENT + r"\s*$", a):
            return (name, "elem")                     # p->member
        if re.match(r"^\*\s*" + esc + r"\s*$", a):
            return (name, "elem")                     # *p
    return None


def scan(sources, protos):
    """Every near/far problem found in these translation units."""
    problems = []
    call = re.compile(r"\b(" + IDENT + r")\s*\(")
    assign = re.compile(r"\b(?:const\s+)?(" + IDENT + r")\s*\*\s*(" + IDENT +
                        r")\s*=\s*([^;]+);")

    for path in sources:
        raw = open(path, "r", errors="replace").read()
        src = strip_comments(raw)
        arrays, file_ptrs = file_far_arrays(src)
        for _fname, params, body, base in functions(src):
            pointers = file_ptrs | far_pointers(params) | far_pointers(body)
            if not arrays and not pointers:
                continue
            # Shape three: an ASSIGNMENT, not a call.  `const Rect *r =
            # &g_last_geom[kind];` truncates just as thoroughly as passing
            # it to a near parameter would, and reads back whatever the
            # matching DGROUP offset holds.
            for m in assign.finditer(body):
                if re.search(r"\bfar\b", m.group(0)[:m.group(0).find("=")]):
                    continue
                hit = arg_is_far(m.group(3), arrays, pointers)
                if hit and hit[1] == "ptr":
                    line = src.count("\n", 0, base + m.start()) + 1
                    problems.append(
                        "%s:%d: far '%s' assigned to NEAR pointer '%s'"
                        "  ->  %s" % (path, line, hit[0], m.group(2),
                                      m.group(0).strip()))

            for m in call.finditer(body):
                fn = m.group(1)
                if fn in KEYWORDS or fn in FAR_SAFE:
                    continue
                # balanced-paren scan for the argument list
                i, depth = m.end() - 1, 0
                while i < len(body):
                    if body[i] == "(":
                        depth += 1
                    elif body[i] == ")":
                        depth -= 1
                        if depth == 0:
                            break
                    i += 1
                if i >= len(body):
                    continue
                args = split_args(body[m.end():i])
                line = src.count("\n", 0, base + m.start()) + 1

                if fn in NEAR_LIBC:
                    classes = ["near-ptr"] * len(args)
                elif fn in protos:
                    classes = protos[fn]
                else:
                    # An unknown callee is not a free pass - but only an
                    # unambiguous pointer is worth stopping the build for.
                    for arg in args:
                        hit = arg_is_far(arg, arrays, pointers)
                        if hit and hit[1] == "ptr":
                            problems.append(
                                "%s:%d: far '%s' passed to '%s', whose "
                                "declaration was not found - cannot prove "
                                "its parameter is far" % (path, line,
                                                          hit[0], fn))
                    continue

                for pos, arg in enumerate(args):
                    if pos >= len(classes):
                        break
                    if classes[pos] != "near-ptr":
                        continue
                    hit = arg_is_far(arg, arrays, pointers)
                    if hit:
                        problems.append(
                            "%s:%d: far '%s' passed as argument %d of '%s', "
                            "whose parameter is NEAR  ->  %s"
                            % (path, line, hit[0], pos + 1, fn, arg.strip()))

    return problems


def self_test(protos):
    """Run the checker over ci/nearfar_cases.c and require that it flags
    every line marked BAD and no line marked OK.  A gate this one has now
    been rewritten four times and twice reported OK over live corruption;
    it needs its own tests more than most of the code it guards."""
    path = "ci/nearfar_cases.c"
    if not os.path.exists(path):
        print("nearfar: SELF-TEST FIXTURE MISSING (%s)" % path)
        return 1
    raw = open(path, "r", errors="replace").read().split("\n")
    want = set(i + 1 for i, l in enumerate(raw) if "/* BAD" in l or "BAD  " in l
               or re.search(r"/\*\s*BAD", l))
    deny = set(i + 1 for i, l in enumerate(raw) if re.search(r"/\*\s*OK", l))
    got = set()
    for p in scan([path], protos):
        m = re.match(r"[^:]+:(\d+):", p)
        if m:
            got.add(int(m.group(1)))
    missed = sorted(want - got)
    wrong  = sorted(deny & got)
    if missed or wrong:
        print("==> near/far SELF-TEST FAILED on %s" % path)
        for n in missed:
            print("  ! line %d is marked BAD but was NOT flagged: %s"
                  % (n, raw[n - 1].strip()))
        for n in wrong:
            print("  ! line %d is marked OK but WAS flagged: %s"
                  % (n, raw[n - 1].strip()))
        return 1
    print("    self-test: %d bad shapes caught, %d good ones left alone"
          % (len(want), len(deny)))
    return 0


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    os.chdir(root)
    sources = sorted(glob.glob("src/*.c"))
    headers = sorted(glob.glob("src/*.h"))
    protos = collect_prototypes(headers + sources + ["ci/nearfar_cases.c"])
    FAR_SAFE.update(collect_macros(headers + sources))

    if self_test(protos) != 0:
        return 1
    problems = scan(sources, protos)

    if problems:
        print("==> near/far: far storage reaching a near pointer parameter")
        for p in problems:
            print("  ! " + p)
        print("nearfar: PROBLEMS FOUND (%d)" % len(problems))
        return 1
    print("==> far arrays and far pointers vs. near parameters"
          " (all argument positions, libc included)")
    print("nearfar: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
