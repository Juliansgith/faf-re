# Engine addition (faf-neural-ai): a copy of faf/gen_sources.py from dependencies/ffmpeg (Draiget/ffmpeg-mirror,
# 8f9ea179334d), unchanged apart from this header and the generated file's first lines, for the native Linux
# client's FFmpeg (cmake/deps/ffmpeg-linux/CMakeLists.txt), which has no make either.
"""Writes faf/<arch>/sources.cmake: the source files of each FFmpeg library, and the
preprocessor and compiler flags, for one configure run.

usage: gen_sources.py <ffmpeg-source-dir> <build-dir>/ffbuild/config.mak <out> <lib>...

FFmpeg's build decides what to compile in GNU make: each library's Makefile adds objects
to OBJS-$(CONFIG_X) / STLIBOBJS-$(CONFIG_X) and so on, configure's config.mak sets the
CONFIG_X / HAVE_X variables to "yes", and ffbuild/common.mak and ffbuild/arch.mak gather
the "-yes" lists. This script evaluates exactly that part of the Makefiles with a small
interpreter for the subset of make they use (variables of both flavours, +=, ?=,
substitution references, a few functions, ifdef/ifeq conditionals and includes), so
that no make is needed and a new FFmpeg version is handled the same way. Rules are
skipped except for target-specific CFLAGS/CPPFLAGS on objects, which are kept.

The per-library sequence is that of the top-level Makefile's DOSUBDIR and
ffbuild/library.mak:
    SUBDIR := lib<name>/
    include lib<name>/Makefile
    -include lib<name>/$(ARCH)/Makefile
    -include lib<name>/$(INTRINSICS)/Makefile
    include ffbuild/arch.mak
    OBJS += $(OBJS-yes), STLIBOBJS += $(STLIBOBJS-yes), SHLIBOBJS += $(SHLIBOBJS-yes)
and the static library is $(OBJS) $(STLIBOBJS) (plus $(SHLIBOBJS) with CONFIG_SHARED).
"""
import os
import re
import sys


class MakeError(Exception):
    pass


class Make:
    def __init__(self, src):
        self.src = src
        self.vars = {}          # name -> (flavour, value); flavour 'r' recursive, 's' simple
        self.target_vars = []   # (targets, name, op, value) from "target: NAME += value"

    # --- expansion ------------------------------------------------------------------
    def get(self, name):
        if name not in self.vars:
            return os.environ.get(name, "") if name in ("PATH",) else ""
        flavour, value = self.vars[name]
        return self.expand(value) if flavour == "r" else value

    @staticmethod
    def _find_close(text, start, open_ch, close_ch):
        depth = 1
        i = start
        while i < len(text):
            c = text[i]
            if c == open_ch:
                depth += 1
            elif c == close_ch:
                depth -= 1
                if depth == 0:
                    return i
            i += 1
        raise MakeError("unbalanced reference: " + text)

    @staticmethod
    def _split_args(text):
        args, depth, cur = [], 0, ""
        for c in text:
            if c in "({":
                depth += 1
            elif c in ")}":
                depth -= 1
            if c == "," and depth == 0:
                args.append(cur)
                cur = ""
            else:
                cur += c
        args.append(cur)
        return args

    @staticmethod
    def _pattern(pat, word):
        """Matches a make pattern (one %) against a word; returns the stem or None."""
        if "%" not in pat:
            return "" if pat == word else None
        pre, post = pat.split("%", 1)
        if word.startswith(pre) and word.endswith(post) and len(word) >= len(pre) + len(post):
            return word[len(pre):len(word) - len(post)]
        return None

    def _patsubst(self, pat, rep, words):
        out = []
        for w in words:
            stem = self._pattern(pat, w)
            if stem is None:
                out.append(w)
            elif "%" in rep:
                out.append(rep.replace("%", stem, 1))
            else:
                out.append(rep)
        return out

    def _function(self, name, rest):
        args = self._split_args(rest)
        x = lambda i: self.expand(args[i]) if i < len(args) else ""
        if name == "sort":
            return " ".join(sorted(set(x(0).split())))
        if name == "strip":
            return " ".join(x(0).split())
        if name == "filter":
            pats = x(0).split()
            return " ".join(w for w in x(1).split() if any(self._pattern(p, w) is not None for p in pats))
        if name == "filter-out":
            pats = x(0).split()
            return " ".join(w for w in x(1).split() if not any(self._pattern(p, w) is not None for p in pats))
        if name == "addprefix":
            return " ".join(x(0) + w for w in x(1).split())
        if name == "addsuffix":
            return " ".join(w + x(0) for w in x(1).split())
        if name == "subst":
            return x(2).replace(x(0), x(1))
        if name == "patsubst":
            return " ".join(self._patsubst(x(0), x(1), x(2).split()))
        if name == "if":
            return x(1) if x(0).strip() else x(2)
        if name == "findstring":
            return x(0) if x(0) in x(1) else ""
        if name == "firstword":
            w = x(0).split()
            return w[0] if w else ""
        if name == "dir":
            return " ".join(w[:w.rfind("/") + 1] or "./" for w in x(0).split())
        if name == "notdir":
            return " ".join(w[w.rfind("/") + 1:] for w in x(0).split())
        if name == "foreach":
            var = x(0).strip()
            saved = self.vars.get(var)
            out = []
            for w in x(1).split():
                self.vars[var] = ("s", w)
                out.append(self.expand(args[2]))
            if saved is None:
                self.vars.pop(var, None)
            else:
                self.vars[var] = saved
            return " ".join(out)
        raise MakeError("unsupported make function: " + name)

    def expand(self, text):
        out = []
        i = 0
        while i < len(text):
            c = text[i]
            if c != "$":
                out.append(c)
                i += 1
                continue
            if i + 1 >= len(text):
                break
            n = text[i + 1]
            if n == "$":
                out.append("$")
                i += 2
                continue
            if n not in "({":
                out.append(self.get(n))  # $@, $<, single-letter variables
                i += 2
                continue
            close = ")" if n == "(" else "}"
            end = self._find_close(text, i + 2, n, close)
            body = text[i + 2:end]
            i = end + 1
            m = re.match(r"([a-z-]+)\s(.*)$", body, re.S)
            if m and m.group(1) in ("sort", "strip", "filter", "filter-out", "addprefix", "addsuffix",
                                    "subst", "patsubst", "if", "findstring", "firstword", "dir",
                                    "notdir", "foreach"):
                out.append(self._function(m.group(1), m.group(2)))
                continue
            if m and m.group(1) in ("eval", "call", "shell", "wildcard", "value", "info",
                                    "warning", "error", "or", "and"):
                raise MakeError("unsupported make function in a variable: " + body)
            # A variable, possibly a substitution reference $(VAR:a=b).
            name = self.expand(body)
            sub = None
            depth = 0
            for k, ch in enumerate(body):
                if ch in "({":
                    depth += 1
                elif ch in ")}":
                    depth -= 1
                elif ch == ":" and depth == 0 and "=" in body[k:]:
                    sub = k
                    break
            if sub is not None:
                name = self.expand(body[:sub])
                a, b = body[sub + 1:].split("=", 1)
                a, b = self.expand(a), self.expand(b)
                words = self.get(name).split()
                if "%" not in a:
                    a, b = "%" + a, "%" + b
                out.append(" ".join(self._patsubst(a, b, words)))
            else:
                out.append(self.get(name))
        return "".join(out)

    # --- parsing --------------------------------------------------------------------
    @staticmethod
    def _logical_lines(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            raw = f.read().replace("\r\n", "\n").split("\n")
        buf = None
        for line in raw:
            if buf is not None:
                line = buf + " " + line.lstrip()
            if line.endswith("\\"):
                buf = line[:-1].rstrip()
                continue
            buf = None
            yield line

    @staticmethod
    def _strip_comment(line):
        depth = 0
        for k, c in enumerate(line):
            if c in "({":
                depth += 1
            elif c in ")}":
                depth -= 1
            elif c == "#" and depth == 0 and (k == 0 or line[k - 1] != "\\"):
                return line[:k]
        return line

    def _cond(self, line):
        word, _, rest = line.strip().partition(" ")
        rest = rest.strip()
        if word in ("ifdef", "ifndef"):
            defined = self.get(self.expand(rest).strip()) != ""
            return defined if word == "ifdef" else not defined
        if word in ("ifeq", "ifneq"):
            if rest.startswith("("):
                a, b = self._split_args(rest[1:rest.rfind(")")])
            else:
                parts = re.findall(r"\"[^\"]*\"|'[^']*'", rest)
                a, b = parts[0][1:-1], parts[1][1:-1]
            equal = self.expand(a).strip() == self.expand(b).strip()
            return equal if word == "ifeq" else not equal
        raise MakeError("not a conditional: " + line)

    def include(self, path, optional=False):
        if not os.path.isfile(path):
            if optional:
                return
            raise MakeError("missing include: " + path)
        stack = []  # (active, taken, parent_active)
        active = True
        in_define = False
        for line in self._logical_lines(path):
            if in_define:
                if line.strip() == "endef":
                    in_define = False
                continue
            if line.startswith("\t"):
                continue  # recipe
            line = self._strip_comment(line).rstrip()
            s = line.strip()
            if not s:
                continue
            first = s.split()[0]
            if first in ("ifdef", "ifndef", "ifeq", "ifneq"):
                result = active and self._cond(s)
                stack.append((active, result))
                active = result
                continue
            if first == "else":
                parent, taken = stack[-1]
                rest = s[4:].strip()
                if rest:  # else ifdef ...
                    result = parent and not taken and self._cond(rest)
                    stack[-1] = (parent, taken or result)
                    active = result
                else:
                    active = parent and not taken
                    stack[-1] = (parent, True)
                continue
            if first == "endif":
                active = stack.pop()[0]
                continue
            if first == "define":
                in_define = True
                continue
            if not active:
                continue
            if first in ("include", "-include", "sinclude"):
                for p in self.expand(s.split(None, 1)[1]).split():
                    self.include(p, optional=first != "include")
                continue
            self._statement(s)
        if stack:
            raise MakeError("unterminated conditional in " + path)

    def _statement(self, s):
        for prefix in ("export ", "override "):
            if s.startswith(prefix):
                s = s[len(prefix):]
        # Find the first top-level ':' or '=' to tell assignments from rules.
        depth = 0
        for k, c in enumerate(s):
            if c in "({":
                depth += 1
            elif c in ")}":
                depth -= 1
            elif depth == 0 and c == "=":
                op = s[k - 1] + "=" if k and s[k - 1] in "+?:" else "="
                name = s[:k - len(op) + 1].strip()
                self.assign(self.expand(name), op, s[k + 1:].strip())
                return
            elif depth == 0 and c == ":":
                if s[k + 1:k + 2] == "=":
                    continue  # := handled at '='
                self._rule(s[:k], s[k + 1:])
                return
        # A bare word (e.g. "$(OBJS):" handled above); anything else is a function call
        # like $(foreach ...) / $(eval ...) at the top level, which these Makefiles only use
        # for rules and help text.

    def _rule(self, targets, rest):
        m = re.match(r"\s*([A-Za-z0-9_]+)\s*(\+=|:=|\?=|=)\s*(.*)$", rest)
        if m and m.group(1) in ("CFLAGS", "CPPFLAGS"):
            self.target_vars.append((self.expand(targets).split(), m.group(1), m.group(2), m.group(3)))

    def assign(self, name, op, value):
        if op == ":=":
            self.vars[name] = ("s", self.expand(value))
        elif op == "?=":
            if name not in self.vars:
                self.vars[name] = ("r", value)
        elif op == "+=":
            if name in self.vars:
                flavour, old = self.vars[name]
                add = self.expand(value) if flavour == "s" else value
                self.vars[name] = (flavour, (old + " " + add).strip() if old else add)
            else:
                self.vars[name] = ("r", value)
        else:
            self.vars[name] = ("r", value)

    def words(self, name):
        return self.get(name).split()


def library_objects(src, config_mak, lib):
    mk = Make(src)
    mk.vars["MAIN_MAKEFILE"] = ("s", "1")
    mk.include(config_mak)
    mk.vars["SRC_PATH"] = ("s", src)
    mk.vars["SUBDIR"] = ("s", "lib%s/" % lib)
    mk.include(os.path.join(src, "lib" + lib, "Makefile"))
    mk.include(os.path.join(src, "lib" + lib, mk.get("ARCH"), "Makefile"), optional=True)
    if mk.get("INTRINSICS"):
        mk.include(os.path.join(src, "lib" + lib, mk.get("INTRINSICS"), "Makefile"), optional=True)
    mk.include(os.path.join(src, "ffbuild", "arch.mak"))
    for v in ("OBJS", "SHLIBOBJS", "STLIBOBJS"):
        mk.assign(v, "+=", "$(%s-yes)" % v)
    sub = "lib%s/" % lib
    objs = set(sub + o for o in mk.words("OBJS")) | set(sub + o for o in mk.words("STLIBOBJS"))
    if mk.get("CONFIG_SHARED"):
        objs |= set(sub + o for o in mk.words("SHLIBOBJS"))
    sources = []
    for obj in sorted(objs):
        base = os.path.normpath(obj[:-2]).replace("\\", "/")
        for ext in (".c", ".S", ".asm", ".cpp"):
            if os.path.isfile(os.path.join(src, base + ext)):
                sources.append(base + ext)
                break
        else:
            raise MakeError("no source for %s" % obj)
    flags = {}
    for targets, var, op, value in mk.target_vars:
        value = mk.expand(value).split()
        for t in targets:
            if t.endswith(".o") and value:
                key = os.path.normpath(t[:-2]).replace("\\", "/")
                flags.setdefault(key, []).extend(value)
    per_source = {s: flags[os.path.splitext(s)[0]] for s in sources if os.path.splitext(s)[0] in flags}
    extralibs = [l[:-4] if l.endswith(".lib") else l[2:] if l.startswith("-l") else l
                 for l in mk.words("EXTRALIBS-" + lib)]
    return sources, per_source, extralibs, mk


def cmake_list(items, indent="  "):
    return "\n".join(indent + i for i in items)


def main():
    src, config_mak, out = sys.argv[1:4]
    libs = sys.argv[4:]
    src = os.path.abspath(src).replace("\\", "/")
    lines = ["# Generated by cmake/deps/ffmpeg-linux/gen_sources.py from FFmpeg's Makefiles and",
             "# configure's ffbuild/config.mak at configure time; do not edit.", ""]
    mk = None
    for lib in libs:
        sources, per_source, extralibs, mk = library_objects(src, config_mak, lib)
        if not mk.get("CONFIG_" + lib.upper()):
            raise MakeError("lib%s is not enabled by this configuration" % lib)
        lines.append("set(FFMPEG_FAF_%s_SOURCES" % lib.upper())
        lines.append(cmake_list(sources))
        lines.append(")")
        lines.append("set(FFMPEG_FAF_%s_SYSTEM_LIBS %s)" % (lib.upper(), " ".join(extralibs)))
        for s, f in sorted(per_source.items()):
            lines.append('set_property(SOURCE "${FFMPEG_FAF_SOURCE_DIR}/%s" APPEND PROPERTY COMPILE_OPTIONS %s)'
                         % (s, " ".join(f)))
        lines.append("")
    # Preprocessor and compiler flags of FFmpeg's compile command ($(CPPFLAGS) $(CFLAGS)).
    # The include directories (-I. -I$(SRC_PATH) plus configure's own) are set by
    # faf/CMakeLists.txt; compiler selection, target, logo and debug-information flags
    # are CMake's.
    cppflags = mk.words("CPPFLAGS")
    defs, incs = [], []
    for f in cppflags:
        if f.startswith("-D"):
            defs.append(f[2:])
        elif f.startswith("-I"):
            p = f[2:]
            if p.startswith(src):
                p = "${FFMPEG_FAF_SOURCE_DIR}" + p[len(src):]
            incs.append(p)
        else:
            raise MakeError("unexpected CPPFLAGS entry: " + f)
    cflags = [f for f in mk.words("CFLAGS")
              if f not in ("-nologo", "-Z7", "-Zi") and not f.startswith("--target=")]
    lines.append("set(FFMPEG_FAF_DEFINITIONS")
    lines.append(cmake_list(defs))
    lines.append(")")
    lines.append("set(FFMPEG_FAF_INCLUDE_DIRECTORIES")
    lines.append(cmake_list('"%s"' % i for i in incs))
    lines.append(")")
    lines.append("set(FFMPEG_FAF_COMPILE_OPTIONS")
    lines.append(cmake_list(cflags))
    lines.append(")")
    with open(out, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    try:
        main()
    except MakeError as e:
        sys.exit("gen_sources.py: %s" % e)
