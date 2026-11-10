#!/usr/bin/env python3
"""Engine addition (dedicated-server plan, cross-standard-library determinism): the faf_sim
sources order nothing through an implementation-defined algorithm outside the reviewed list.

Windows builds the engine against the MSVC STL, Linux against libc++. The two agree on what the
standard specifies and differ on what it leaves open:

  * std::sort, std::partial_sort, std::nth_element and std::partition are not stable: elements
    that compare equal come out in an implementation-defined order. The heap algorithms
    (make_heap, push_heap, pop_heap, sort_heap, std::priority_queue) are likewise free to place
    equal elements differently.
  * std::unordered_map / std::unordered_set iterate in bucket order, which depends on std::hash
    (identity in libc++, FNV-1a in the MSVC STL) and on the bucket-count policy (primes in
    libc++, powers of two in the MSVC STL).

A sort with ties, or an iteration whose order reaches sim state, the sim checksum or a
Lua-visible order, desyncs a Linux server from Windows clients. legacy/algorithms/Sort.h
(fa::sort, the recovered MSVC8 introsort) is the same code on both, and so is a comparator that
is a strict total order.

This gate computes the faf_sim translation units the way cmake/FafLinux.cmake does (src/sources.cmake,
src/sim_sources.cmake, the FILTER EXCLUDE lines, the remote sources and the extra target_sources),
follows their quoted #includes inside src/, and lists every

  sort     a call to one of the non-stable or heap algorithms above, or a std::priority_queue;
  iterate  a range-for over, or a begin()/cbegin() of, a name declared with an unordered type;

then fails on any site that tests/linux/order_allowlist.txt does not name. The allowlist keys a
site by "<path> | <kind> | <first line of the statement, whitespace collapsed>", so moving code
does not invalidate it and changing the call does. Each entry carries the audit's class
(safe, not-sim, fix-needed); docs/SIM_ORDER_AUDIT.md has the reasons.

Unordered iteration is found by name: what is declared with an unordered type (directly, through a
using/typedef alias, or as `auto x = <unordered name>`) in the files a translation unit sees. An
iteration through an unnamed temporary or a template parameter is not seen; the audit covers those
by hand.

Usage: OrderGate.py <allowlist> [--root <repo>] [--strict] [--list]
  --strict  also fail on fix-needed entries and on allowlist entries that match no site.
  --list    print every site with its key and class, and exit 0.
"""
import argparse
import os
import re
import sys

SORT_ALGORITHMS = ("sort", "partial_sort", "partial_sort_copy", "nth_element", "partition",
                   "make_heap", "push_heap", "pop_heap", "sort_heap")
CLASSES = ("safe", "not-sim", "fix-needed")

SORT_CALL = re.compile(r"\bstd\s*::\s*(?:ranges\s*::\s*)?(" + "|".join(SORT_ALGORITHMS) + r")\s*(?:<[^;(){}]*>)?\s*\(")
PRIORITY_QUEUE = re.compile(r"\bstd\s*::\s*priority_queue\s*<")
UNORDERED_TYPE = re.compile(r"\bstd\s*::\s*unordered_(?:map|set|multimap|multiset)\s*<")
MEMBER_BEGIN = re.compile(r"(?:\.|->)\s*c?begin\s*\(")
FREE_BEGIN = re.compile(r"(?<![\w.>])(?:std\s*::\s*)?c?begin\s*\(([^()]*)\)")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)


# --- the faf_sim translation units -----------------------------------------------------------

def cmake_text(path):
    """A CMake file without its comment lines and trailing comments outside quotes."""
    out = []
    for line in open(path, encoding="utf-8", errors="replace"):
        result, quoted = [], False
        for ch in line:
            if ch == '"':
                quoted = not quoted
            if ch == "#" and not quoted:
                break
            result.append(ch)
        out.append("".join(result))
    return "".join(out)


def cmake_block(text, opener):
    """The arguments of the first `<opener> ... )` block, as raw tokens (quotes kept)."""
    tokens = []
    for token in re.compile(r'"(?:[^"\\]|\\.)*"|\)|[^\s")]+').finditer(text, text.index(opener) + len(opener)):
        if token.group(0) == ")":
            return tokens
        tokens.append(token.group(0))
    return tokens


def cmake_regex(token):
    return re.compile(token.strip('"').replace("\\\\", "\\"))


def sim_translation_units(root):
    src = os.path.join(root, "src")
    engine = cmake_block(cmake_text(os.path.join(src, "sources.cmake")), "set(FAF_ENGINE_SOURCES")
    sim_cmake = cmake_text(os.path.join(src, "sim_sources.cmake"))
    client_only = [cmake_regex(t) for t in cmake_block(sim_cmake, "set(_faf_client_only_patterns")]
    sim_patterns = [cmake_regex(t) for t in cmake_block(sim_cmake, "set(_faf_sim_patterns")]
    kept = [s for s in engine
            if not any(p.search(s) for p in client_only) or any(p.search(s) for p in sim_patterns)]

    linux = cmake_text(os.path.join(root, "cmake", "FafLinux.cmake"))
    for token in re.findall(r'list\(FILTER FAF_SIM_SOURCES EXCLUDE REGEX ("(?:[^"\\]|\\.)*")\)', linux):
        pattern = cmake_regex(token)
        kept = [s for s in kept if not pattern.search(s)]

    deps = os.path.join(root, "dependencies")
    files = []
    for s in kept:
        s = s.replace("${FAF_DEPENDENCIES_DIR}", deps)
        files.append(s if os.path.isabs(s) else os.path.join(src, s))

    remote = cmake_text(os.path.join(src, "remote", "sources.cmake"))
    for name in ("FAF_REMOTE_SOURCES", "FAF_REMOTE_CLIENT_SOURCES"):
        if name in linux:
            files += [os.path.join(src, s) for s in cmake_block(remote, "set(" + name)]

    for block in re.findall(r"target_sources\(faf_sim PRIVATE([^)]*)\)", linux):
        for token in re.findall(r'"([^"]+)"', block):
            files.append(token.replace("${PROJECT_SOURCE_DIR}", root))

    return [os.path.normpath(f) for f in files if f.endswith((".cpp", ".cc", ".c"))]


# --- reading C++ -------------------------------------------------------------------------------

LEXEME = re.compile(
    r"//[^\n]*"                                   # line comment
    r"|/\*.*?(?:\*/|\Z)"                          # block comment
    r'|(?<!\w)(?:u8|u|U|L)?R"([^(\s"]*)\(.*?\)\1"'  # raw string
    r'|"(?:\\.|[^"\\\n])*"'                       # string
    r"|(?<![\w'])'(?:\\.|[^'\\\n])*'",            # char literal (not a digit separator)
    re.S)


def strip_code(text):
    """The text with comments blanked and string/char literal contents emptied, newlines kept."""
    def blank(m):
        lexeme = m.group(0)
        body = re.sub(r"[^\n]", " ", lexeme)
        if lexeme[0] in "/":
            return body
        quote = '"' if lexeme.endswith('"') else "'"
        return quote + body[2:] + quote if len(lexeme) >= 2 else body
    return LEXEME.sub(blank, text)


def matching(text, pos, open_ch, close_ch):
    """Index just past the bracket that closes the one at text[pos]."""
    depth = 0
    for j in range(pos, len(text)):
        if text[j] == open_ch:
            depth += 1
        elif text[j] == close_ch:
            depth -= 1
            if depth == 0:
                return j + 1
    return len(text)


def template_end(text, pos):
    """Index just past the '>' closing the template argument list opened at text[pos] == '<'."""
    depth = 0
    for j in range(pos, len(text)):
        ch = text[j]
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
            if depth == 0:
                return j + 1
        elif ch in ";{}":
            return j
    return len(text)


class Source:
    def __init__(self, path, src_dir):
        self.path = path
        raw = open(path, encoding="utf-8", errors="replace").read()
        self.code = strip_code(raw)
        self.line_starts = [0] + [m.end() for m in re.finditer("\n", self.code)]
        self.includes = []
        here = os.path.dirname(path)
        for name in INCLUDE.findall(raw):
            for base in (here, src_dir, os.path.join(src_dir, "platform", "posix", "win32")):
                candidate = os.path.normpath(os.path.join(base, name))
                if candidate.startswith(src_dir) and os.path.isfile(candidate):
                    self.includes.append(candidate)
                    break

    def line_of(self, pos):
        lo, hi = 0, len(self.line_starts) - 1
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if self.line_starts[mid] <= pos:
                lo = mid
            else:
                hi = mid - 1
        return lo + 1

    def fingerprint(self, pos):
        """The allowlist key's text: from the start of pos's line to the first '{' or ';' after pos
        (so a call split over lines keeps its arguments), whitespace collapsed, at most 200 chars."""
        start = self.line_starts[self.line_of(pos) - 1]
        end = pos
        while end < len(self.code) and end - start < 400 and self.code[end] not in "{;":
            end += 1
        return " ".join(self.code[start:end + 1].split())[:200]


def unordered_declarations(code, alias_types):
    """Names declared with an unordered type, and new aliases of one, in this file."""
    names, aliases = set(), set()
    if "unordered_" not in code and not any(a in code for a in alias_types):
        return names, aliases
    type_starts = [m.start() for m in UNORDERED_TYPE.finditer(code)]
    for name in alias_types:
        type_starts += [m.start() for m in re.finditer(r"(?<![\w:])" + re.escape(name) + r"\b(?!\s*::)", code)]
    for start in type_starts:
        lt = code.find("<", start)
        end = template_end(code, lt) if lt >= 0 and code[start:lt].strip().endswith(
            ("map", "set", "multimap", "multiset")) else start + len(re.match(r"\w+", code[start:]).group(0))
        tail = code[end:end + 160]
        m = re.match(r"(?:\s*(?:const|&|\*|&&))*\s*(\w+)\s*([;=({,\[)]|$)", tail)
        if m and m.group(1) not in ("const", "operator", "iterator", "const_iterator"):
            names.add(m.group(1))
        head = code[max(0, start - 200):start]
        alias = re.search(r"\busing\s+(\w+)\s*=\s*(?:typename\s+)?$", head)
        if alias:
            aliases.add(alias.group(1))
        if re.search(r"\btypedef\s+(?:typename\s+)?$", head):
            m = re.match(r"\s*(\w+)\s*;", tail)
            if m:
                aliases.add(m.group(1))
    return names, aliases


def range_for_ranges(code):
    """(position, range expression) of every range-for."""
    for m in re.finditer(r"\bfor\s*\(", code):
        open_paren = m.end() - 1
        close = matching(code, open_paren, "(", ")")
        header = code[open_paren + 1:close - 1]
        depth = 0
        for k, ch in enumerate(header):
            if ch in "(<[{":
                depth += 1
            elif ch in ")>]}":
                depth -= 1
            elif ch == ";" and depth == 0:
                break
            elif ch == ":" and depth == 0 and header[k - 1:k] != ":" and header[k + 1:k + 2] != ":":
                yield m.start(), header[k + 1:].strip()
                break


def last_name(expr):
    """The identifier an expression ends in (`a.b->c` -> c, `f()` -> f, `*p` -> p)."""
    expr = expr.strip()
    if expr.endswith(")"):  # a call: the callee's name
        depth = 0
        for k in range(len(expr) - 1, -1, -1):
            depth += {")": 1, "(": -1}.get(expr[k], 0)
            if depth == 0:
                expr = expr[:k].rstrip()
                break
    m = re.search(r"(\w+)\s*$", expr)
    return m.group(1) if m else None


NOT_TYPES = {"return", "else", "case", "throw", "delete", "new", "typename", "goto", "co_return",
             "co_yield", "sizeof", "const", "volatile", "static", "using", "namespace", "struct", "class"}


def unordered_here(code, pos, name, names, aliases):
    """Whether `name` at pos is an unordered container, judged by its nearest declaration in this
    file before pos. No declaration here (a member or a global from a header): True."""
    pattern = re.compile(
        r"(?<![\w.>])(?P<type>[A-Za-z_][\w:]*(?:\s*<[^;{}]*?>)?)(?:\s*\bconst\b)?(?:\s*[&*]+\s*|\s+)"
        r"(?:\bconst\b\s*[&*]*\s*)?" + re.escape(name) + r"\b(?=\s*(?:[;=({,)\[]|:(?!:)))")
    last = None
    for m in pattern.finditer(code, 0, pos):
        if m.group("type").split("<")[0].strip() not in NOT_TYPES:
            last = m
    if last is None:
        return True
    declared_type = last.group("type")
    if "unordered_" in declared_type or any(re.search(r"\b" + re.escape(a) + r"\b", declared_type) for a in aliases):
        return True
    if declared_type == "auto":
        rest = code[last.end():last.end() + 400]
        init = re.match(r"\s*=\s*([^;]*);", rest)
        if not init:
            return True  # a range-for or structured binding: judge by name
        return "unordered_" in init.group(1) or last_name(init.group(1)) in names
    return False


def find_sites(root):
    src_dir = os.path.normpath(os.path.join(root, "src"))
    units = sim_translation_units(root)
    sources = {}

    def load(path):
        if path not in sources:
            sources[path] = Source(path, src_dir) if os.path.isfile(path) else None
        return sources[path]

    closures = {}
    for unit in units:
        seen, stack = {}, [unit]
        while stack:
            path = stack.pop()
            if path in seen or load(path) is None:
                continue
            seen[path] = True
            stack.extend(sources[path].includes)
        closures[unit] = list(seen)

    # Unordered names per file: declarations in the file itself, with aliases resolved across
    # every scanned file (aliases are few and distinctively named).
    aliases = set()
    while True:
        found = set()
        for s in sources.values():
            if s is not None:
                found |= unordered_declarations(s.code, aliases)[1]
        if found <= aliases:
            break
        aliases |= found
    declared = {p: unordered_declarations(s.code, aliases)[0] for p, s in sources.items() if s is not None}
    # `auto& x = <unordered name>;` and `auto x = <unordered name>` carry the type to a new name.
    for p, s in sources.items():
        if s is None:
            continue
        for m in re.finditer(r"\bauto\s*[&*]*\s*(\w+)\s*=\s*([^;]+);", s.code):
            if last_name(m.group(2)) in declared[p]:
                declared[p].add(m.group(1))

    visible = {}  # file -> unordered names visible from any unit that includes it
    for unit, closure in closures.items():
        names = set().union(*(declared[p] for p in closure))
        for p in closure:
            visible.setdefault(p, set()).update(names)

    sites = {}
    for path in sorted(visible):
        s = sources[path]
        rel = os.path.relpath(path, root).replace(os.sep, "/")

        def add(kind, pos, what):
            key = f"{rel} | {kind} | {s.fingerprint(pos)}"
            sites.setdefault(key, []).append((rel, s.line_of(pos), what))

        for m in SORT_CALL.finditer(s.code):
            add("sort", m.start(), "std::" + m.group(1))
        for m in PRIORITY_QUEUE.finditer(s.code):
            add("sort", m.start(), "std::priority_queue")
        names = visible[path]
        if not names:
            continue
        def unordered(name, pos):
            return name in names and unordered_here(s.code, pos, name, names, aliases)

        for pos, expr in range_for_ranges(s.code):
            name = last_name(expr)
            if unordered(name, pos):
                add("iterate", pos, "range-for over " + name)
        for m in MEMBER_BEGIN.finditer(s.code):
            owner = re.search(r"(\w+)\s*(?:\(\s*\))?\s*$", s.code[max(0, m.start() - 120):m.start()])
            if owner and unordered(owner.group(1), m.start() - len(owner.group(0))):
                add("iterate", m.start() - len(owner.group(0)), owner.group(1) + ".begin()")
        for m in FREE_BEGIN.finditer(s.code):
            name = last_name(m.group(1))
            if name and unordered(name, m.start()):
                add("iterate", m.start(), "begin(" + m.group(1).strip() + ")")
    return units, sites


# --- the gate ----------------------------------------------------------------------------------

def load_allowlist(path):
    allowed = {}
    for number, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        cls, _, key = line.partition(" ")
        if cls not in CLASSES:
            sys.exit(f"{path}:{number}: class '{cls}' is not one of {', '.join(CLASSES)}")
        allowed[" ".join(key.split())] = cls
    return allowed


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("allowlist")
    parser.add_argument("--root", default=os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")))
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    units, sites = find_sites(os.path.abspath(args.root))
    allowed = load_allowlist(args.allowlist)

    if args.list:
        for key in sorted(sites):
            for rel, line, what in sites[key]:
                print(f"{allowed.get(key, 'UNLISTED'):10} {rel}:{line}  {what}")
            print(f"           key: {key}")
        return 0

    unexpected = [key for key in sorted(sites) if key not in allowed]
    pending = [key for key in sorted(sites) if allowed.get(key) == "fix-needed"]
    stale = [key for key in sorted(allowed) if key not in sites]

    if unexpected:
        print("order gate: implementation-defined ordering not in tests/linux/order_allowlist.txt:")
        for key in unexpected:
            for rel, line, what in sites[key]:
                print(f"  {rel}:{line}  {what}")
            print(f"    add after review:  <safe|not-sim|fix-needed> {key}")
    if stale:
        print(f"order gate: {len(stale)} allowlist entr{'y matches' if len(stale) == 1 else 'ies match'} no site (remove):")
        for key in stale:
            print("  " + key)
    if pending:
        print(f"order gate: {len(pending)} reviewed site(s) still marked fix-needed (docs/SIM_ORDER_AUDIT.md)")
    if unexpected or (args.strict and (stale or pending)):
        return 1
    print(f"order gate: {len(units)} translation units, {len(sites)} reviewed ordering sites, none unreviewed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
