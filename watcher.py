#!/usr/bin/env python3
"""
GX - watcher.py
---------------
External helper for the "GX - Gold Export" WoW addon.

WHY THIS EXISTS
---------------
The WoW Lua sandbox cannot write arbitrary files (no io/os file API). An addon
can only flush data through SavedVariables, and the client only writes those to
disk on "/reload" or logout - never live while playing. This script lives
OUTSIDE the game, watches the addon's SavedVariables file (WTF\\Account\\...\\
SavedVariables\\GX.lua) and re-writes a clean single-line text file every time
the game writes new data. OBS can then use that text file directly via a
"Text (GDI+)" source with "Read from file" enabled.

Requirements: Python 3.6+ (stdlib only, no third-party packages).
"""

import argparse
import os
import re
import sys
import time

TABLE_NAME_RE = re.compile(r'^\s*\["([^"]+)"\]\s*=\s*\{')
TABLE_CLOSE_RE = re.compile(r'^\s*\},?\s*$')
GOLD_VALUE_RE = re.compile(r'^\s*\["gold"\]\s*=\s*(\d+)')

DEFAULT_POLL_SECONDS = 2.0
DEFAULT_OUTPUT = "totalgold.txt"


def format_gold(copper):
    """Format a copper amount like WoW's money display: '1,234g 56s 78c'."""
    copper = max(0, int(copper))
    gold, rem = divmod(copper, 10000)
    silver, c = divmod(rem, 100)
    parts = []
    if gold:
        parts.append(f"{gold:,}g")
    if silver:
        parts.append(f"{silver}s")
    if c or not parts:
        parts.append(f"{c}c")
    return " ".join(parts)


def parse_gold_file(path):
    """Sum character gold + Warband bank gold from SavedVariables GX.lua.

    Tracks nested table context so that ["gold"] values under
    ``["warband"]`` are counted separately from character entries.
    Any gold value found outside a ``warband`` table is treated as
    character gold (backward-compatible with older SavedVariables).
    """
    characters_gold = 0
    warband_gold = 0
    tables = []
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if TABLE_CLOSE_RE.match(line):
                if tables:
                    tables.pop()
                continue
            opened = TABLE_NAME_RE.match(line)
            if opened:
                tables.append(opened.group(1))
                continue
            gold = GOLD_VALUE_RE.match(line)
            if not gold:
                continue
            amount = int(gold.group(1))
            if "warband" in tables:
                warband_gold += amount
            else:
                characters_gold += amount
    return characters_gold + warband_gold


def find_wow_root(start_dir):
    """Walk UP from start_dir until a directory containing WTF\\Account is found.

    Works no matter where the script sits inside the WoW install (addon folder,
    install root, ether of the flavor folders...). Returns None if not found.
    """
    current = os.path.abspath(start_dir)
    while True:
        if os.path.isdir(os.path.join(current, "WTF", "Account")):
            return current
        parent = os.path.dirname(current)
        if parent == current:
            return None
        current = parent


def discover_file(wow_root, account_filter=None):
    """Look for WTF\\Account\\*\\SavedVariables\\GX.lua under a WoW install."""
    # Accept either the base install or one of the client flavor folders
    # (_retail_ / _classic_ / _classic_era_ / _ptr_).
    roots = [wow_root]
    for flavor in ("_retail_", "_classic_", "_classic_era_", "_ptr_"):
        roots.append(os.path.join(wow_root, flavor))

    found = []
    for root in roots:
        base = os.path.join(root, "WTF", "Account")
        if not os.path.isdir(base):
            continue
        try:
            accounts = sorted(os.listdir(base))
        except OSError:
            continue
        for account in accounts:
            if account_filter and account != account_filter:
                continue
            sv_dir = os.path.join(base, account, "SavedVariables")
            if not os.path.isdir(sv_dir):
                continue
            candidate = os.path.join(sv_dir, "GX.lua")
            if candidate not in found:
                found.append(candidate)
    return found


def find_file_hint():
    """Return a human-readable pointer to where the file usually lives."""
    return (
        "It is normally located here (Windows):\n"
        "    C:\\Users\\<you>\\Documents\\World of Warcraft\\_retail_\\WTF\\"
        "Account\\<YourAccountName>\\SavedVariables\\GX.lua\n"
        "or wherever your WoW install sits: "
        "<WOW_ROOT>\\WTF\\Account\\<Account>\\SavedVariables\\GX.lua\n"
        "NOTE: the file is ONLY created by the game after the addon has loaded "
        "once. Log into a character, run /gx show, then /reload, then retry this "
        "script.\n"
        "\n"
        "It is also auto-detected: run the script from ANY folder inside the WoW "
        "install (the addon folder works) and it will walk up to WTF itself."
    )


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            "Watch the GX addon SavedVariables file and write the summed "
            "account gold to a text file for OBS."
        )
    )
    parser.add_argument(
        "--file",
        metavar="PATH",
        default=None,
        help="Full path to SavedVariables\\GX.lua. If omitted, --wow-root is used.",
    )
    parser.add_argument(
        "--wow-root",
        metavar="PATH",
        default=None,
        help="WoW install root (folder containing WTF). Used to auto-discover GX.lua.",
    )
    parser.add_argument(
        "--account",
        metavar="NAME",
        default=None,
        help="Restrict discovery to one WTF account folder, e.g. '410566417#1'. "
             "Useful when more than one account exists on this install.",
    )
    parser.add_argument(
        "--output",
        metavar="PATH",
        default=DEFAULT_OUTPUT,
        help=f"Output text file (default: {DEFAULT_OUTPUT!r}).",
    )
    parser.add_argument(
        "--poll",
        metavar="SECONDS",
        type=float,
        default=DEFAULT_POLL_SECONDS,
        help="Polling interval in seconds (default: 2.0).",
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Write only the raw copper number instead of the formatted string.",
    )
    parser.add_argument(
        "--compact",
        action="store_true",
        help="Clean minimal display showing only current gold in a small terminal window.",
    )
    args = parser.parse_args(argv)

    def render_compact(gold_str, sub=None):
        if os.name == "nt":
            os.system("cls")
        else:
            sys.stdout.write("\033[2J\033[H")
            sys.stdout.flush()
        print("")
        print(f"  {gold_str}")
        if sub:
            print(f"  Updated: {sub}")
        print("")

    if not args.file:
        # Auto-discover: explicit --wow-root first, then walk up from the
        # script location / current working directory to find WTF\Account.
        candidates = discover_file(args.wow_root, args.account) if args.wow_root else []
        if not candidates:
            auto_root = find_wow_root(os.path.dirname(os.path.abspath(__file__)))
            if not auto_root and not args.wow_root:
                auto_root = find_wow_root(os.getcwd())
            if auto_root:
                if not args.compact:
                    print(f"Auto-detected WoW root: {auto_root}")
                candidates = discover_file(auto_root, args.account)
        if len(candidates) > 1 and not args.compact:
            print(
                "Multiple GX.lua files found under WTF, using the first:\n"
                + "\n".join(f"  - {c}" for c in candidates)
            )
        if candidates:
            args.file = candidates[0]

    if not args.file:
        if args.compact:
            render_compact("ERROR: cannot locate GX.lua", "Run /gx show and /reload in WoW")
        else:
            print("ERROR: cannot locate GX.lua. Pass --file <path> or --wow-root <path>.")
            print(find_file_hint())
        return 1

    args.file = os.path.abspath(args.file)
    args.output = os.path.abspath(args.output)

    if not args.compact:
        print(f"Watching: {args.file}")
        print(f"Account folder: {os.path.basename(os.path.dirname(os.path.dirname(args.file)))}")
        print(f"Output:   {args.output}")
        print(f"Polling:  every {args.poll:g} s   (raw mode: {args.raw})")

    if not os.path.isfile(args.file):
        if args.compact:
            render_compact("Waiting for SavedVariables...", "Log into WoW and run /reload")
        else:
            print("The file does not exist yet. Waiting for the game to write its "
                  "first SavedVariables (log in once / run /gx show, then /reload).")

    poll = max(args.poll, 0.5)
    last_stat = None
    last_value = None

    try:
        while True:
            try:
                stat = os.stat(args.file)
            except FileNotFoundError:
                stat = None

            if stat and (stat.st_mtime_ns, stat.st_size) != last_stat:
                value = None
                for attempt in range(3):  # retry: WoW may be mid-write
                    try:
                        value = parse_gold_file(args.file)
                        break
                    except (OSError, ValueError):
                        time.sleep(1.0)
                if value is not None and value != last_value:
                    last_value = value
                    last_stat = (stat.st_mtime_ns, stat.st_size)
                    if args.raw:
                        text = str(value)
                    else:
                        text = format_gold(value)
                    try:
                        # "w" mode: overwrite, never append. One line, newline at end.
                        with open(args.output, "w", encoding="utf-8", newline="\n") as out:
                            out.write(text + "\n")
                        if args.compact:
                            render_compact(text, time.strftime("%H:%M:%S"))
                        else:
                            print(f"[{time.strftime('%H:%M:%S')}] total gold = {text}")
                    except OSError as exc:
                        if args.compact:
                            render_compact(text, f"ERROR: {exc}")
                        else:
                            print(f"[{time.strftime('%H:%M:%S')}] ERROR writing "
                                  f"{args.output}: {exc}")
            elif stat:
                if last_stat is None:
                    last_stat = (stat.st_mtime_ns, stat.st_size)

            time.sleep(poll)
    except KeyboardInterrupt:
        if args.compact and os.name == "nt":
            os.system("cls")
        sys.exit(0)


if __name__ == "__main__":
    sys.exit(main())