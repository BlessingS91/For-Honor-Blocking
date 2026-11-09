#!/usr/bin/env python3
"""Batch-add early telegraph annotations to Skyrim SE animation HKX files.

External dependency: HKXC Anno CLI (hkxc-anno-cli.exe) and its hkxc.exe.
Input HKX files are NEVER modified; output is a separate directory.
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

TIME_RE = re.compile(r"^\s*(\d+(?:\.\d+)?)\s+(.+?)\s*$")
COUNT_RE = re.compile(r"^(\s*#\s*numAnnotations\s*:\s*)(\d+)(.*)$", re.IGNORECASE)
HEADER_RE = re.compile(r"^\s*#\s*(?:trackName|numAnnotations)\s*:", re.IGNORECASE)
DURATION_RE = re.compile(r"^\s*#\s*duration\s*:\s*([0-9]+(?:\.[0-9]+)?)", re.IGNORECASE)
EPSILON = 0.00015  # HKX dump writes times to six decimal places


@dataclass
class Event:
    time: float
    name: str
    order: int


@dataclass
class Parsed:
    comments: list[str]
    events: list[Event]
    count_prefix: str | None
    count_suffix: str
    duration: float | None


@dataclass
class Stats:
    added: int = 0
    already_present: int = 0
    conflicts: int = 0
    moved: int = 0
    clamped: int = 0
    triggers: int = 0
    hit_pairs: int = 0
    swings_moved: int = 0
    swings_already_aligned: int = 0
    swing_clamped: int = 0
    prehit_added: int = 0
    prehit_replaced: int = 0
    prehit_duplicates_removed: int = 0
    prehit_unchanged: int = 0
    prehit_clamped: int = 0


def parse_annotation_text(txt: str) -> Parsed:
    comments: list[str] = []
    events: list[Event] = []
    count_prefix: str | None = None
    count_suffix = ""
    count_value: int | None = None
    duration: float | None = None
    track_headers = 0
    for line_num, raw in enumerate(txt.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            comments.append(raw)
            if HEADER_RE.match(raw):
                track_headers += 1 if "trackName" in raw or "trackname" in raw.lower() else 0
            m = COUNT_RE.match(raw)
            if m:
                if count_value is not None:
                    raise ValueError("Multiple # numAnnotations headers: paired/multi-track HKX not supported safely")
                count_prefix, count_value, count_suffix = m.group(1), int(m.group(2)), m.group(3)
            dm = DURATION_RE.match(raw)
            if dm:
                duration = float(dm.group(1))
            continue
        m = TIME_RE.match(raw)
        if not m:
            raise ValueError(f"Unknown annotation text format on line {line_num}: {raw[:100]!r}")
        t, name = float(m.group(1)), m.group(2)
        if not math.isfinite(t) or t < 0:
            raise ValueError(f"Invalid timestamp on line {line_num}")
        events.append(Event(t, name, len(events)))
    if count_value is not None and len(events) != count_value:
        raise ValueError(f"Header says {count_value} annotations but parsed {len(events)}; refusing to rewrite")
    if track_headers > 1:
        raise ValueError("Multiple annotation tracks in dump; refusing to rewrite")
    if duration is not None and any(e.time > duration + EPSILON for e in events):
        raise ValueError("Event timestamp exceeds animation duration")
    return Parsed(comments, events, count_prefix, count_suffix, duration)


def transform(parsed: Parsed, *, right_trigger: str, left_trigger: str,
              right_event: str, left_event: str, right_ms: float, left_ms: float,
              replace_existing: bool, align_swings: bool = False,
              hit_trigger: str = "HitFrame", swing_before_hit_ms: float = 200.0,
              set_prehitframe: bool = False, prehitframe_ms: float = 16.667) -> tuple[str, Stats]:
    """Optionally retime all swings from hit annotations, then place early indicators.

    Hit annotations do not encode left/right. Pair every right/left swing with
    the corresponding hit annotation by chronological order in the HKX track.
    Refuse ambiguous files with unequal counts rather than guess a pairing.
    """
    events = [Event(e.time, e.name, e.order) for e in parsed.events]
    stats = Stats()
    # Preserve the original ordering/positions for pairing *existing* indicators.
    # An indicator authored before the old swing must not become an undetected
    # duplicate if the swing moves to an earlier timestamp than that indicator.
    original_time = {e.order: e.time for e in events}

    if align_swings:
        swing_names = {right_trigger.casefold(), left_trigger.casefold()}
        swing_events = sorted((e for e in events if e.name.casefold() in swing_names),
                              key=lambda e: (e.time, e.order))
        hit_events = sorted((e for e in events if e.name.casefold() == hit_trigger.casefold()),
                            key=lambda e: (e.time, e.order))
        if len(swing_events) != len(hit_events):
            raise ValueError(
                f"Cannot align swings: {len(swing_events)} weapon swing event(s) but "
                f"{len(hit_events)} {hit_trigger} event(s). "
                "Each swing needs one hit annotation (paired in time order)."
            )
        for swing, hit in zip(swing_events, hit_events):
            stats.hit_pairs += 1
            expected = hit.time - swing_before_hit_ms / 1000.0
            if expected < 0:
                expected = 0.0
                stats.swing_clamped += 1
            if abs(swing.time - expected) <= EPSILON:
                stats.swings_already_aligned += 1
            else:
                swing.time = expected
                stats.swings_moved += 1

    rules = ((right_trigger, right_event, right_ms), (left_trigger, left_event, left_ms))
    for trigger_name, output_name, lead_ms in rules:
        source_events = sorted((e for e in events if e.name.casefold() == trigger_name.casefold()),
                               key=lambda e: (original_time[e.order], e.order))
        last_source = -1.0
        for source in source_events:
            stats.triggers += 1
            expected = source.time - lead_ms / 1000.0
            if expected < 0:
                expected = 0.0
                stats.clamped += 1
            # Pair existing indicators using the ORIGINAL swing timestamps.
            # This preserves and detects authored events even if the swing moves
            # earlier than them in the new alignment pass.
            source_old_time = original_time[source.order]
            preceding = [e for e in events if e.name.casefold() == output_name.casefold()
                         and e.order in original_time
                         and original_time[e.order] > last_source + EPSILON
                         and original_time[e.order] <= source_old_time + EPSILON]
            if any(abs(e.time - expected) <= EPSILON for e in preceding):
                stats.already_present += 1
            elif preceding:
                if replace_existing and len(preceding) == 1:
                    preceding[0].time = expected
                    stats.moved += 1
                else:
                    stats.conflicts += 1
            else:
                events.append(Event(expected, output_name, len(events)))
                stats.added += 1
            last_source = source_old_time
    if set_prehitframe:
        if not math.isfinite(prehitframe_ms) or prehitframe_ms < 0:
            raise ValueError("preHitFrame time must be a nonnegative finite number of milliseconds")
        desired = prehitframe_ms / 1000.0  # Fixed time from animation start.
        if parsed.duration is not None and desired > parsed.duration:
            desired = parsed.duration
            stats.prehit_clamped += 1

        # Independent of --replace-existing: there must be exactly ONE
        # preHitFrame in every processed animation, regardless of swing events.
        existing = [e for e in events if e.name.casefold() == "prehitframe"]
        if (len(existing) == 1 and existing[0].name == "preHitFrame"
                and round(existing[0].time, 6) == round(desired, 6)):
            stats.prehit_unchanged += 1
        else:
            next_order = max((e.order for e in events), default=-1) + 1
            events = [e for e in events if e.name.casefold() != "prehitframe"]
            events.append(Event(desired, "preHitFrame", next_order))
            if existing:
                stats.prehit_replaced += 1
                stats.prehit_duplicates_removed += max(0, len(existing) - 1)
            else:
                stats.prehit_added += 1

    events.sort(key=lambda e: (e.time, e.order))
    comments = []
    for line in parsed.comments:
        m = COUNT_RE.match(line)
        if m:
            comments.append(f"{m.group(1)}{len(events)}{m.group(3)}")
        else:
            comments.append(line)
    # Put animation metadata/comments AFTER all chronological annotations.
    output = "\n".join([f"{e.time:.6f} {e.name}" for e in events] + comments) + "\n"
    return output, stats


def run_cli(executable: str, arguments: list[str], *, timeout: int = 120) -> None:
    result = subprocess.run([executable, *arguments], capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=timeout)
    if result.returncode != 0:
        details = (result.stderr or result.stdout or "no diagnostic output").strip()
        raise RuntimeError(f"HKXC command exited {result.returncode}: {details[-750:]}")


def find_executable(arg: str) -> str:
    p = Path(arg).expanduser()
    if p.is_file():
        return str(p.resolve())
    found = shutil.which(arg)
    if not found:
        raise FileNotFoundError(f"Cannot locate {arg!r}. Supply --tool with the full path to hkxc-anno-cli.exe")
    return found


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Add pre-swing indicator annotations to animation HKX files.")
    ap.add_argument("--input", required=True, type=Path, help="Folder containing .hkx animations, recursive")
    ap.add_argument("--output", required=True, type=Path, help="Separate folder for patched .hkx files")
    ap.add_argument("--tool", default="hkxc-anno-cli.exe", help="Path to HKXC Anno CLI executable")
    ap.add_argument("--lead-ms", type=float, default=150.0, help="Default lead before swing, milliseconds")
    ap.add_argument("--right-lead-ms", type=float, help="Override right attack lead time, ms")
    ap.add_argument("--left-lead-ms", type=float, help="Override left attack lead time, ms")
    ap.add_argument("--right-trigger", default="weaponSwing", help="Existing right-side swing event")
    ap.add_argument("--left-trigger", default="weaponLeftSwing", help="Existing left-side swing event")
    ap.add_argument("--right-event", default="indicatorStart", help="New right-side UI event")
    ap.add_argument("--left-event", default="LeftIndicatorStart", help="New left-side UI event")
    ap.add_argument("--align-swings", action="store_true", help="Place weaponSwing/weaponLeftSwing before ordered HitFrame events")
    ap.add_argument("--swing-before-hit-ms", type=float, default=200.0,
                    help="Lead from each swing to its paired hit annotation, ms (when --align-swings)")
    ap.add_argument("--hit-trigger", default="HitFrame", help="Existing hit annotation name, case-insensitive")
    ap.add_argument("--set-prehitframe", action="store_true",
                    help="Ensure exactly one preHitFrame at a fixed time in every animation")
    ap.add_argument("--prehitframe-ms", type=float, default=16.667,
                    help="Fixed preHitFrame timestamp from animation start in milliseconds (default: 16.667)")
    ap.add_argument("--replace-existing", action="store_true", help="Move ONE existing indicator paired to a swing to the new offset")
    ap.add_argument("--write", action="store_true", help="Actually produce output HKXs (default dry-run)")
    ap.add_argument("--overwrite", action="store_true", help="Allow replacing files within the output directory")
    ap.add_argument("--no-verify", action="store_true", help="Skip dumping the generated HKX for verification")
    ap.add_argument("--progress", action="store_true", help="Emit progress records for graphical frontends")
    ap.add_argument("--cancel-file", type=Path, default=None, help="Stop safely between HKX files when this file appears")
    args = ap.parse_args(argv)

    lead_right = args.right_lead_ms if args.right_lead_ms is not None else args.lead_ms
    lead_left = args.left_lead_ms if args.left_lead_ms is not None else args.lead_ms
    if not all(math.isfinite(x) and x >= 0 for x in (lead_right, lead_left, args.swing_before_hit_ms)):
        ap.error("lead times must be nonnegative, finite milliseconds")
    if args.set_prehitframe and (not math.isfinite(args.prehitframe_ms) or args.prehitframe_ms < 0):
        ap.error("preHitFrame time must be nonnegative finite milliseconds")
    if not args.right_trigger or not args.left_trigger or not args.right_event or not args.left_event or not args.hit_trigger:
        ap.error("event names must not be empty")
    if args.align_swings and len({args.right_trigger.casefold(), args.left_trigger.casefold(),
                                  args.hit_trigger.casefold()}) != 3:
        ap.error("right swing, left swing, and hit annotations must have distinct names")
    root, output = args.input.expanduser().resolve(), args.output.expanduser().resolve()
    if not root.is_dir():
        ap.error(f"input directory does not exist: {root}")
    if output == root or root in output.parents or output in root.parents:
        ap.error("input and output directories must be independent; never overwrite source animations")
    try:
        tool = find_executable(args.tool)
    except FileNotFoundError as err:
        ap.error(str(err))

    targets = sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() == ".hkx")
    print(f"Mode: {'WRITE' if args.write else 'DRY RUN'} | {len(targets)} HKX files | right={lead_right:g}ms left={lead_left:g}ms"
          + (f" | swing={args.swing_before_hit_ms:g}ms before {args.hit_trigger} (ordered pairing)"
             if args.align_swings else "")
          + (f" | preHitFrame={args.prehitframe_ms:g}ms" if args.set_prehitframe else ""))
    counts = {"changed": 0, "unchanged": 0, "failed": 0, "added": 0, "moved": 0, "conflicts": 0, "clamped": 0}
    if args.align_swings:
        counts.update({"swing_moved": 0, "swing_aligned": 0, "swing_clamped": 0, "paired": 0})
    if args.set_prehitframe:
        counts.update({"prehit_added": 0, "prehit_replaced": 0,
                       "prehit_duplicates_removed": 0, "prehit_unchanged": 0, "prehit_clamped": 0})
    cancelled = False
    for index, src in enumerate(targets, 1):
        if args.cancel_file is not None and args.cancel_file.is_file():
            cancelled = True
            print("CANCELLED: Stop requested; finished the active file and left input files untouched.", flush=True)
            break
        relative = src.relative_to(root)
        if args.progress:
            print(f"PROGRESS {index}/{len(targets)} | {relative}", flush=True)
        dest = output / relative
        try:
            if args.write and dest.exists() and not args.overwrite:
                print(f"[{index}/{len(targets)}] SKIP output exists: {relative} (pass --overwrite)")
                counts["unchanged"] += 1
                continue
            with tempfile.TemporaryDirectory(prefix="fhb_annos_") as folder:
                folder = Path(folder)
                anno = folder / "dump.txt"
                run_cli(tool, ["dump", "-i", str(src), "-o", str(anno)])
                if not anno.is_file():
                    raise RuntimeError("CLI dump did not generate the expected annotation file")
                orig = parse_annotation_text(anno.read_text(encoding="utf-8-sig", errors="replace"))
                rewritten, stats = transform(
                    orig, right_trigger=args.right_trigger, left_trigger=args.left_trigger,
                    right_event=args.right_event, left_event=args.left_event,
                    right_ms=lead_right, left_ms=lead_left, replace_existing=args.replace_existing,
                    align_swings=args.align_swings, hit_trigger=args.hit_trigger,
                    swing_before_hit_ms=args.swing_before_hit_ms,
                    set_prehitframe=args.set_prehitframe, prehitframe_ms=args.prehitframe_ms,
                )
                counts["added"] += stats.added
                counts["moved"] += stats.moved
                counts["conflicts"] += stats.conflicts
                counts["clamped"] += stats.clamped
                if args.align_swings:
                    counts["swing_moved"] += stats.swings_moved
                    counts["swing_aligned"] += stats.swings_already_aligned
                    counts["swing_clamped"] += stats.swing_clamped
                    counts["paired"] += stats.hit_pairs
                if args.set_prehitframe:
                    counts["prehit_added"] += stats.prehit_added
                    counts["prehit_replaced"] += stats.prehit_replaced
                    counts["prehit_duplicates_removed"] += stats.prehit_duplicates_removed
                    counts["prehit_unchanged"] += stats.prehit_unchanged
                    counts["prehit_clamped"] += stats.prehit_clamped
                summary = f"{relative} | swing={stats.triggers} add={stats.added} move={stats.moved} existing={stats.already_present} conflicts={stats.conflicts} clamped={stats.clamped}"
                if args.align_swings:
                    summary += (f" | hits={stats.hit_pairs} swing_move={stats.swings_moved} "
                                f"swing_aligned={stats.swings_already_aligned} "
                                f"swing_clamped={stats.swing_clamped}")
                if args.set_prehitframe:
                    summary += (f" | prehit_add={stats.prehit_added} prehit_replace={stats.prehit_replaced} "
                                f"prehit_duplicates_removed={stats.prehit_duplicates_removed} "
                                f"prehit_aligned={stats.prehit_unchanged} prehit_clamped={stats.prehit_clamped}")
                if not (stats.added or stats.moved or stats.swings_moved or
                        stats.prehit_added or stats.prehit_replaced):
                    counts["unchanged"] += 1
                    if stats.triggers or stats.conflicts:
                        print("UNCHANGED", summary)
                    continue
                if not args.write:
                    counts["changed"] += 1
                    print("WOULD PATCH", summary)
                    continue
                anno.write_text(rewritten, encoding="utf-8", newline="\n")
                dest.parent.mkdir(parents=True, exist_ok=True)
                # Never rewrite input: HKXC CLI gets explicit -o and temporary staging output.
                staged = folder / "patched.hkx"
                run_cli(tool, ["update", "-i", str(src), "-a", str(anno), "-v", "amd64", "-o", str(staged)])
                if not staged.is_file() or staged.stat().st_size == 0:
                    raise RuntimeError("CLI update did not produce nonempty HKX")
                if not args.no_verify:
                    verification = folder / "verify.txt"
                    run_cli(tool, ["dump", "-i", str(staged), "-o", str(verification)])
                    verified = parse_annotation_text(verification.read_text(encoding="utf-8-sig", errors="replace"))
                    expected_pairs = sorted((round(e.time, 4), e.name) for e in parse_annotation_text(rewritten).events)
                    actual_pairs = sorted((round(e.time, 4), e.name) for e in verified.events)
                    if expected_pairs != actual_pairs:
                        raise RuntimeError("Re-dump verification mismatch; did not copy patched file")
                shutil.copy2(staged, dest)
                counts["changed"] += 1
                print("PATCHED", summary)
        except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as exc:
            counts["failed"] += 1
            print(f"ERROR {relative}: {exc}", file=sys.stderr)
    print("\nSummary:", ", ".join(f"{k}={v}" for k, v in counts.items()))
    if not args.write:
        print("This was a dry run. Add --write after reviewing the proposed changes.")
    if args.align_swings and counts["swing_clamped"]:
        print("WARNING: Some swings were clamped to t=0 because their hit annotation occurs too early for the specified lead.")
    if counts["clamped"]:
        print("WARNING: Some indicators were clamped to t=0 because the swing occurs earlier than the requested lead.")
    if args.set_prehitframe and counts["prehit_clamped"]:
        print("WARNING: Some preHitFrame timestamps were clamped to the animation duration.")
    if counts["conflicts"]:
        print("WARNING: Existing indicators were left unchanged. Use --replace-existing to reposition uniquely paired indicators.")
    if counts["failed"]:
        print("WARNING: Some files failed. No input HKX files were modified.", file=sys.stderr)
    return 130 if cancelled else (1 if counts["failed"] else 0)


if __name__ == "__main__":
    raise SystemExit(main())
