#!/usr/bin/env python3
"""Check that the tests wait for what they expect, not for a fixed time.

A fixed wait is slow, and on a busy computer too short: a test waits for the
thing itself (``wait_until``, ``QTest::qWaitFor``, ``QSignalSpy::wait``).
Some waits are fixed on purpose: they give what must not happen the time to,
or let time pass that the code under test measures. Each file keeps exactly
the ones ALLOWED counts for it, of 200 ms or more, for the reason given; one
more is an error, and so is one fewer, so that a wait gone frees no room for
another. The browser tests' own rule is tests/remote_web/README.md's.

A fixed wait is any of:

- ``qWait``, ``qSleep``, ``msleep``, ``sleep_for`` or ``run_events_for``;
- a loop on a clock alone, ``while (timer.elapsed() < 300) { ... }``, that
  neither returns nor breaks;
- a wait for what never comes, ``wait_until([] { return false; }, 300)``,
  or for a clock, ``wait_until([&] { return clock.elapsed() >= 300; })``,
  or ``settle_while([] { return true; }, 300)``;
- a call of a helper whose fixed wait is as long as one of its arguments
  (``settle(a, b, 1500)``, ``raw.run({&c})``): it counts where the helper is
  called, at that argument or its default.

A length that is not written out counts too, as it may be long.

    python3 tools/test_sleeps.py         # exit 1 and list each file off its count
    python3 tools/test_sleeps.py --list  # each fixed wait, and how long
"""
from __future__ import annotations

import pathlib
import re
import sys

from _check import blank

ROOT = pathlib.Path(__file__).resolve().parent.parent
LONGEST = 200  # ms
# What the waits are made of: its waits are its callers'.
HELPERS = "test_wait.hpp"

# Each file's fixed waits, and why they are fixed.
ALLOWED = {
    "bench_hot_paths.cpp": (1, "run by hand: seven seconds of rounds, the slowest timed"),
    "bench_presenter_view.cpp": (1, "run by hand: two seconds of windows left alone"),
    "test_audio_engine.cpp": (1, "past the half second a player counts the mixer gone after"),
    "test_discovery.cpp": (21, "Reach's retries, back-off and forgetting, on intervals the tests "
                               "shorten, and what must not be heard"),
    "test_facade_media.cpp": (1, "the thumbnail warmer's look for a file not there, which changes nothing"),
    "test_facade_playback.cpp": (3, "a paused clock and a background that must not move or stop, and a "
                                    "thread kept busy on purpose"),
    "test_ffmpeg_live_input.cpp": (2, "a rate in pictures a second, and an input held open while it blocks"),
    "test_library_sync.cpp": (4, "the join's wait while files keep coming, a pause that must hold, and "
                                 "snapshots looked at as they are written"),
    "test_link.cpp": (73, "a raw peer reads what comes for a while (RawPeer::run), what must not be "
                          "asked for or come, connections let in, limited and timed out on the link's "
                          "own clock, and the hidden benches"),
    "test_ndi_output.cpp": (1, "the keepalive ticks of a real NDI sender, which say nothing"),
    "test_remote_server.cpp": (7, "a patch that must not reach another device, a token's two seconds, "
                                  "a request held while another arrives, and one not drawn before its time"),
    "test_shipped_bibles.cpp": (1, "a copy of the shipped Bibles that must not be taken"),
    "test_server.cpp": (2, "a starting server polled, and a pause that must not come"),
    "test_sync_app.cpp": (20, "the holds and pauses the sync waits out, an edit nothing says was seen, "
                              "what is refused without a word, the window let draw, and the hidden "
                              "screenshots"),
    "test_throttle.cpp": (2, "a call that must not come, and a rate over a second"),
    "test_video_player.cpp": (2, "a paused clock that must not move, and four seconds of play, each call "
                                 "timed"),
    "test_video_with_sound.cpp": (6, "stretches of play over which lip sync is measured, each as long as "
                                     "it says"),
    "test_webengine.cpp": (3, "what Chromium must not draw over the page, in the hidden tests CI's "
                              "webengine job runs"),
}

CALLS = re.compile(r"\b(qWait|qSleep|msleep|sleep_for|run_events_for)\s*\(")
LOOPS = re.compile(r"\bwhile\s*\(\s*\w+\.elapsed\(\)\s*<\s*([\w:']+)\s*\)\s*\{")
PREDICATE_WAITS = re.compile(r"\b(wait_until|qWaitFor|settle_while)\s*\(")
NEVER = {"wait_until": "false", "qWaitFor": "false", "settle_while": "true"}
CLOCK = re.compile(r"^return\s+\w+\.elapsed\(\)\s*>=?\s*(.+?)\s*;?$")
LAMBDA = re.compile(r"^\[[^\]]*\]\s*(?:\(\s*\))?\s*(?:mutable\s*)?\{(.*)\}$", re.S)
NOT_FUNCTIONS = {"if", "while", "for", "switch", "catch", "return", "sizeof"}


def closing(text: str, at: int) -> int:
    """Where the bracket opened at ``at`` closes."""
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack = []
    for k in range(at, len(text)):
        if text[k] in pairs:
            stack.append(pairs[text[k]])
        elif stack and text[k] == stack[-1]:
            stack.pop()
            if not stack:
                return k
    return len(text) - 1


def arguments(text: str, open_paren: int) -> list[str]:
    """The arguments of the call whose ``(`` is at ``open_paren``."""
    end = closing(text, open_paren)
    args, depth, start = [], 0, open_paren + 1
    for k in range(open_paren + 1, end):
        if text[k] in "([{":
            depth += 1
        elif text[k] in ")]}":
            depth -= 1
        elif text[k] == "," and depth == 0:
            args.append(text[start:k].strip())
            start = k + 1
    last = text[start:end].strip()
    if last or args:
        args.append(last)
    return args


def length(expr: str) -> int | str | None:
    """``expr`` in ms; its name when it is one; None when it is not written out."""
    expr = expr.strip()
    m = re.fullmatch(r"(?:std::chrono::)?(microseconds|milliseconds|seconds)\s*[({]\s*([\d']+)\s*[)}]", expr)
    if m:
        n = int(m.group(2).replace("'", ""))
        return {"microseconds": n // 1000, "milliseconds": n, "seconds": n * 1000}[m.group(1)]
    m = re.fullmatch(r"([\d']+)\s*(ms|s)?", expr)
    if m:
        return int(m.group(1).replace("'", "")) * (1000 if m.group(2) == "s" else 1)
    if re.fullmatch(r"[A-Za-z_]\w*", expr):
        return expr
    m = re.fullmatch(r"[^?]+\?([^:]+):(.+)", expr)
    if m:
        a, b = length(m.group(1)), length(m.group(2))
        if isinstance(a, int) and isinstance(b, int):
            return max(a, b)
    return None


def enclosing_parameter(text: str, at: int, name: str):
    """The function or lambda around ``at`` that takes ``name``: its name,
    the parameter's index and its default (None for none); None when none
    does."""
    depth = 0
    for k in range(at, -1, -1):
        if text[k] == "}":
            depth += 1
        elif text[k] == "{":
            if depth:
                depth -= 1
                continue
            head = text[max(0, k - 400):k].rstrip()
            head = re.sub(r"(?:\b(?:const|noexcept|override|mutable)\b\s*)*$", "", head).rstrip()
            if not head.endswith(")"):
                continue
            # The ( that matches the last ).
            open_paren, level = 0, 0
            for j in range(len(head) - 1, -1, -1):
                if head[j] == ")":
                    level += 1
                elif head[j] == "(":
                    level -= 1
                    if level == 0:
                        open_paren = j
                        break
            before = head[:open_paren].rstrip()
            m = re.search(r"(\w+)$", before)
            if m and m.group(1) not in NOT_FUNCTIONS:
                fn = m.group(1)
            else:
                m = re.search(r"\b(\w+)\s*=\s*\[[^\]]*\]$", before)
                if not m:
                    continue
                fn = m.group(1)
            params = arguments(head, open_paren)
            for index, param in enumerate(params):
                p = re.fullmatch(r"(?:const\s+)?(?:int|qint64|std::chrono::milliseconds)\s+(\w+)(?:\s*=\s*(.+))?",
                                 param)
                if p and p.group(1) == name:
                    return fn, index, p.group(2)
    return None


def waits(text: str) -> list[tuple[int, int | str | None]]:
    """Each fixed wait in ``text`` (blanked): where, and how long."""
    found = []
    for m in CALLS.finditer(text):
        args = arguments(text, m.end() - 1)
        found.append((m.start(), length(args[0]) if args else None))
    for m in LOOPS.finditer(text):
        body = text[m.end() - 1:closing(text, m.end() - 1)]
        if not re.search(r"\b(?:return|break)\b", body):
            found.append((m.start(), length(m.group(1))))
    for m in PREDICATE_WAITS.finditer(text):
        args = arguments(text, m.end() - 1)
        if not args:
            continue
        lam = LAMBDA.match(args[0])
        if not lam:
            continue
        body = " ".join(lam.group(1).split())
        timeout = length(args[1]) if len(args) > 1 else 20000
        if body in (f"return {NEVER[m.group(1)]};", f"return {NEVER[m.group(1)]}"):
            found.append((m.start(), timeout))
        elif m.group(1) != "settle_while":
            clock = CLOCK.match(body)
            if clock:
                found.append((m.start(), length(clock.group(1))))
    return found


def fixed_waits(files: dict[str, str]) -> dict[str, list[tuple[int, int | None]]]:
    """Each file's fixed waits of LONGEST or more, of ``files`` (by name,
    their text): the line of each, and its length (None: not written out)."""
    blanked = {name: blank(text) for name, text in files.items() if name != HELPERS}
    out: dict[str, list[tuple[int, int | None]]] = {name: [] for name in blanked}
    wrappers: set[tuple[str, str]] = set()  # (file, helper)
    todo = [(name, at, ms) for name, text in blanked.items() for at, ms in waits(text)]
    while todo:
        name, at, ms = todo.pop()
        if isinstance(ms, str):
            around = enclosing_parameter(blanked[name], at, ms)
            if around:
                fn, index, default = around
                if (name, fn) not in wrappers:
                    wrappers.add((name, fn))
                    todo.extend(calls(files, blanked, name, fn, index, default))
                continue
            ms = None
        if ms is None or ms >= LONGEST:
            out[name].append((blanked[name].count("\n", 0, at) + 1, ms))
    return {name: sorted(found) for name, found in out.items()}


def calls(files: dict[str, str], blanked: dict[str, str], where: str, fn: str, index: int,
          default: str | None):
    """The calls of helper ``fn`` (in ``where``), as waits: in that file, and
    in those that include it when it is a header."""
    include = re.compile(r'#include\s+"' + re.escape(where) + '"')
    pattern = re.compile(r"(?<![\w:])" + re.escape(fn) + r"\s*\(")
    for name, text in blanked.items():
        if name != where and not (where.endswith(".hpp") and include.search(files[name])):
            continue
        for m in pattern.finditer(text):
            args = arguments(text, m.end() - 1)
            typed = re.search(r"(\w+)\s+$", text[:m.start()])
            if typed and typed.group(1) not in ("return", "else"):
                continue  # its definition or declaration, after its type
            given = args[index] if index < len(args) else default
            yield name, m.start(), length(given) if given is not None else None


def main() -> int:
    files = {path.name: path.read_text(encoding="utf-8") for path in sorted((ROOT / "tests").glob("*.[ch]pp"))}
    failed = False
    for name, found in sorted(fixed_waits(files).items()):
        if "--list" in sys.argv:
            for line, ms in found:
                print(f"tests/{name}:{line}: {'?' if ms is None else ms} ms")
            continue
        allowed = ALLOWED.get(name, (0, ""))[0]
        if len(found) != allowed:
            failed = True
            print(f"tests/{name}: {len(found)} fixed waits of {LONGEST} ms or more, {allowed} allowed")
    for name in sorted(set(ALLOWED) - set(files)):
        failed = True
        print(f"tests/{name} is allowed fixed waits, and is not there")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
