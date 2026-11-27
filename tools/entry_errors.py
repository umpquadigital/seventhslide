#!/usr/bin/env python3
"""Check that what src/app shows for a failure is in words for people.

What the operator, or a phone on the remote, is shown for a failure is a
sentence for people (docs/CODING.md, src/app/entry_error.hpp). Two ways back
from that are easy to write and look like the rest. This fails on either:

* ``what()`` (``ex.what()``, ``e->what()``), in src/app and src/remote,
  anywhere but inside a log: a call of qWarning, qCritical, qInfo, qDebug or
  qFatal (or their qC forms) or its stream, or a ``std::cerr <<`` statement,
  either of them after an ``if (...)``, ``else`` or loop head. An
  exception's own words are the log's; report_entry_error (or
  message_for_operator) shows the operator a plain sentence for it.
* in src/app, a refusal written as a literal rather than with tr() (or a
  function of refusals.hpp). A literal is a plain string or one made a
  QString as it is (``QStringLiteral``, ``QString``, ``QLatin1String``,
  ``u"..."_s``), adjacent strings joined, and with what follows it
  (``.arg(...)``); either side of a ``?:`` counts. It fails where the
  literal is
  - returned (a ``QStringLiteral`` or other QString literal of two words or
    more: "No database"; a plain string returned is a name as often as not);
  - an argument, in any place, of a call that refuses or fails
    (``refuse...``, ``refusal``, ``fail...``, ``errorOccurred``, or a name
    ending in ``error``: ``set_error``, ``set_last_error``; not
    ``report_entry_error``, whose first argument is the entry point's name
    for the log);
  - a map's ``"error"``, ``"errors"`` or ``"message"`` (``{key, value}``,
    ``insert(key, value)``, ``map[key] = value``, a QVariantMap's or a
    nlohmann::json's);
  - assigned or added to something named ``error``, ``errors``, ``why``,
    ``problem`` or ``refusal``, or ending in ``_error``, ``_errors`` or
    ``Error`` (``last_error``, ``*error_out``).
  These hold any word ("Stopped"); a literal with none (``"%1: %2"``) is a
  composition, not a refusal. What is inside a string (a QML snippet in a
  raw string) is not code and is not checked.

ALLOWED names the files whose refusal literals are not shown to anyone, and
why; an allowance that matches nothing fails too.

    python3 tools/entry_errors.py    # exit 1 and list each one found
"""
import re
import sys

from _check import blank, line_of, report, source_files, unused

SUFFIXES = {".cpp", ".hpp", ".h", ".mm"}

# Each file whose refusal literals no one is shown, and why.
ALLOWED = {
    # The thumbnail caches' error_out is read by tests alone: the image
    # providers and the media controller pass none, and a thumbnail that
    # cannot be made is left out and logged (entry_error.hpp).
    "src/app/media_thumb_cache.cpp": "a thumbnail's error_out, which only tests read",
    "src/app/freeform_thumb_cache.cpp": "a thumbnail's error_out, which only tests read",
}

# --- what() -----------------------------------------------------------------

WHAT = re.compile(r"(?:\.|->)\s*what\s*\(\s*\)")
LOG_CALLS = {"qWarning", "qCritical", "qInfo", "qDebug", "qFatal",
             "qCWarning", "qCCritical", "qCInfo", "qCDebug"}
CALL_NAME = re.compile(r"([A-Za-z_]\w*)\s*$")
LOG_STATEMENT = re.compile(r"(?:std::cerr\b|(?:%s)\s*\()" % "|".join(sorted(LOG_CALLS)))
# What may stand before a statement's own words: a control head, or ``else``.
HEAD = re.compile(r"(?:else\b|(?:if|for|while|switch)\s*\()\s*")


def enclosing_calls(code: str, pos: int) -> list[str]:
    """The names of the calls whose parentheses ``pos`` is inside, innermost
    first, back to the start of its statement."""
    names, depth = [], 0
    i = pos
    while i > 0:
        i -= 1
        c = code[i]
        if c == ")":
            depth += 1
        elif c == "(":
            if depth == 0:
                m = CALL_NAME.search(code, 0, i)
                names.append(m.group(1) if m else "")
            else:
                depth -= 1
        elif c in ";{}" and depth == 0:
            break
    return names


def statement_start(code: str, pos: int) -> str:
    """The first words of the statement ``pos`` is in."""
    i = pos
    depth = 0
    while i > 0:
        i -= 1
        c = code[i]
        if c == ")":
            depth += 1
        elif c == "(":
            depth -= 1
        elif c in ";{}" and depth <= 0:
            break
    return code[i + 1:pos].lstrip()


def without_heads(start: str) -> str:
    """``start`` past the control heads before it: ``if (x) qWarning()``
    is ``qWarning()``."""
    while m := HEAD.match(start):
        i = m.end()
        if m.group(0).startswith("else"):
            start = start[i:]
            continue
        depth = 1
        while i < len(start) and depth:
            depth += {"(": 1, ")": -1}.get(start[i], 0)
            i += 1
        start = start[i:].lstrip()
    return start


def what_problems(rel: str, text: str) -> list[str]:
    code = blank(text)
    lines = text.split("\n")
    out = []
    for m in WHAT.finditer(code):
        if LOG_CALLS & set(enclosing_calls(code, m.start())):
            continue
        if LOG_STATEMENT.match(without_heads(statement_start(code, m.start()))):
            continue  # a stream: qWarning() << ..., std::cerr << ...
        n = line_of(text, m.start())
        out.append(f"{rel}:{n}: {lines[n - 1].strip()} (what() is for the log: report_entry_error)")
    return out


# --- refusals ---------------------------------------------------------------

STRINGS = r'(?:(?:u8|u|L)?"(?:[^"\\\n]|\\.)*"\s*)+'
# A literal at the start of an expression, made a QString as it is or not.
LITERAL = re.compile(
    r"(?P<wrap>(?:QStringLiteral|QString(?:::fromUtf8|::fromLatin1)?|QLatin1String(?:View)?"
    r"|QLatin1StringView|QStringView)\s*\(\s*)?(?P<strs>" + STRINGS + r")")
PIECE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
WORD = re.compile(r"[A-Za-z]{2,}")
# Words, from the start: not markup ("<svg viewBox=...") or a format.
SENTENCE = re.compile(r"^(?=[A-Za-z%])[^<>{}]*?[A-Za-z]{2,}\s+[A-Za-z]")

CALLS = re.compile(r"\b(?P<name>\w*refus\w*|\w*fail\w*|errorOccurred|\w*_error|\w*Error)\s*\(")
NOT_REFUSING = {"report_entry_error"}
# An exception made (throw std::runtime_error("..."), std::system_error(...)):
# its words are what(), the log's, which the first part checks are logged.
THROWN = re.compile(r"(?:\bthrow\s+(?:[\w:]+::)?|\bstd::)$")
# A pair's two names ({std::pair{"error", "error"}, ...}) is not a map's value.
PAIRS = {"pair", "make_pair"}
NAMED = re.compile(r"(?:^(?:errors?|why|problem|refusal)|_errors?|Errors?)(?:_out|_)?$")
KEPT = re.compile(
    r"(?P<name>\b[A-Za-z_]\w*)\s*(?:\.\s*(?:push_back|append|emplace_back|insert)\s*\(|"
    r"(?<![=!<>+\-*/%&|^])=(?!=)|\+=|<<)")
MAP_KEY = re.compile(
    r"(?:QStringLiteral|QString|QLatin1String(?:View)?|QLatin1StringView)?\s*\(?\s*"
    r'"(?P<key>error|errors|message)"\s*\)?\s*(?P<to>,|\]\s*=(?!=))')


def literal_text(expr: str) -> tuple[str, bool] | None:
    """The text of the literal ``expr`` starts with, and whether it is made a
    QString; None when it starts with none."""
    m = LITERAL.match(expr)
    if not m:
        return None
    qstring = bool(m.group("wrap")) or expr[m.end():].startswith(("_s", "_L1", "_qs"))
    return "".join(PIECE.findall(m.group("strs"))), qstring


def until(code: str, start: int, stops: str) -> int:
    """Where the expression from ``start`` ends: at the first of ``stops``
    outside brackets, or where a bracket it is in closes."""
    depth = 0
    for i in range(start, len(code)):
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            if depth == 0:
                return i
            depth -= 1
        elif depth == 0 and c in stops:
            return i
    return len(code)


def split_top(code: str, start: int, end: int, at: str) -> list[tuple[int, int]]:
    """``code[start:end]`` cut at each ``at`` outside brackets (``?`` and a
    ``:`` that is not ``::`` for a conditional)."""
    parts, depth, i, last = [], 0, start, start
    while i < end:
        c = code[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif depth == 0 and c in at:
            if c == ":" and (code[i + 1:i + 2] == ":" or code[i - 1:i] == ":"):
                i += 1
            else:
                parts.append((last, i))
                last = i + 1
        i += 1
    parts.append((last, end))
    return parts


def opened_by(code: str, pos: int) -> str:
    """The name before the bracket ``pos`` is inside (``pair`` for
    ``std::pair{...}``), or ""."""
    depth = 0
    for i in range(pos - 1, -1, -1):
        c = code[i]
        if c in ")]}":
            depth += 1
        elif c in "([{":
            if depth == 0:
                m = CALL_NAME.search(code, 0, i)
                return m.group(1) if m else ""
            depth -= 1
    return ""


def values(code: str, start: int, end: int) -> list[int]:
    """Where each value ``code[start:end]`` can come to starts: the
    expression, both sides of a conditional, the items of a list
    (``QStringList{...}``, ``{...}``)."""
    parts = split_top(code, start, end, "?:")
    if len(parts) > 1:
        parts = parts[1:]  # the condition is not a value
    out = []
    for a, b in parts:
        while a < b and code[a].isspace():
            a += 1
        brace = re.match(r"(?:QStringList\s*)?\{", code[a:b])
        if brace:
            inner = a + brace.end()
            close = until(code, inner, "")
            for x, y in split_top(code, inner, close, ","):
                out += values(code, x, y)
        else:
            out.append(a)
    return out


def refusal_problems(rel: str, text: str) -> list[str]:
    # Comments go; strings stay, as what is checked is in them. ``bare`` has
    # the strings' insides blanked too: what is found inside a string (a
    # QML snippet) is not code, and brackets in a string are not brackets.
    kept = blank(text, strings=False)
    bare = blank(text)
    lines = text.split("\n")
    found = set()

    def in_code(pos: int) -> bool:
        return bare[pos] == kept[pos]

    def check(place: str, starts: list[int], sentence: bool = False) -> None:
        for at in starts:
            got = literal_text(kept[at:at + 2000])
            if got is None:
                continue
            literal, qstring = got
            if sentence and not (qstring and SENTENCE.search(literal)):
                continue
            if WORD.search(literal):
                found.add((line_of(text, at), place))

    def arguments(open_paren: int) -> list[int]:
        close = until(bare, open_paren + 1, "")
        starts = []
        for a, b in split_top(bare, open_paren + 1, close, ","):
            starts += values(bare, a, b)
        return starts

    # return QStringLiteral("No database"); return ok ? QString() : QString("...");
    for m in re.finditer(r"\breturn\b", bare):
        end = until(bare, m.end(), ";")
        check("returned as a sentence", values(bare, m.end(), end), sentence=True)

    # refuse(QStringLiteral("...")), errorOccurred("..."), set_error(out, "...")
    for m in CALLS.finditer(bare):
        if m.group("name") in NOT_REFUSING or THROWN.search(bare[max(0, m.start() - 80):m.start()]):
            continue
        check("passed to a call that refuses", arguments(m.end() - 1))

    # {QStringLiteral("error"), "..."}, insert("errors", QStringList{...}),
    # out[QStringLiteral("error")] = "...", nlohmann::json{{"error", "..."}}
    for m in MAP_KEY.finditer(kept):
        if not in_code(m.start("key") - 1) or opened_by(bare, m.start()) in PAIRS:
            continue
        end = until(bare, m.end(), ",;")
        check("set as a map's error", values(bare, m.end(), end))

    # why = "..."; out.errors.push_back("..."); last_error = "..."; *error_out = "...";
    for m in KEPT.finditer(bare):
        if not NAMED.search(m.group("name")):
            continue
        if m.group(0).rstrip().endswith("("):
            check("kept as an error", arguments(m.end() - 1))
        else:
            check("kept as an error", values(bare, m.end(), until(bare, m.end(), ";,")))

    return [f"{rel}:{n}: {lines[n - 1].strip()} (a refusal {place}: tr(), or refusals.hpp's)"
            for n, place in sorted(found)]


def main() -> int:
    problems = []
    for rel, text in source_files(["src/app", "src/remote"], SUFFIXES):
        problems += what_problems(rel, text)
    used = set()
    for rel, text in source_files(["src/app"], SUFFIXES):
        found = refusal_problems(rel, text)
        if rel not in ALLOWED:
            problems += found
        elif found:
            used.add(rel)
    problems += unused("entry_errors.py", [rel for rel in ALLOWED if rel not in used])
    return report("entry errors", problems)


if __name__ == "__main__":
    sys.exit(main())
