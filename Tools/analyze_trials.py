#!/usr/bin/env python3
"""Summarise interruption trials from Saved/ResearchLogs/*.jsonl.

Usage:
    python Tools/analyze_trials.py                 # all logs
    python Tools/analyze_trials.py <file-or-dir>   # one file / one folder
    python Tools/analyze_trials.py --csv out.csv

One row per trial (events between trial_started and trial_ended). Logs without trial markers
(schema 1, before 2026-11-13) are reported as one row per session with trial="(session)".
The script only counts what the log states; nothing is inferred from text.
"""
import csv
import glob
import json
import os
import sys
from datetime import datetime


def parse_time(event):
    try:
        return datetime.fromisoformat(event["timestamp_utc"].replace("Z", "+00:00")).timestamp()
    except Exception:
        return None


def load(path):
    events = []
    with open(path, encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                print(f"  ! {os.path.basename(path)} line {line_number}: not valid JSON, skipped", file=sys.stderr)
    return events


def split_trials(events):
    """Yield (trial_id, condition, events). Falls back to the whole session when no trial markers exist."""
    if not any(e.get("event_type") == "trial_started" for e in events):
        yield "(session)", "", events
        return
    current, trial_id, condition = None, "", ""
    for event in events:
        kind = event.get("event_type")
        if kind == "trial_started":
            current, trial_id, condition = [event], event.get("trial_id", ""), event.get("condition", "")
        elif current is not None:
            current.append(event)
            if kind == "trial_ended":
                yield trial_id, condition, current
                current = None
    if current:
        yield trial_id + " (no end marker)", condition, current


def summarise(session_file, trial_id, condition, events):
    def of(kind):
        return [e for e in events if e.get("event_type") == kind]

    def payload(e):
        return e.get("payload") or {}

    row = {
        "file": os.path.basename(session_file),
        "trial": trial_id,
        "condition": condition,
        "duration_s": "",
        "player_utterances": len(of("player_utterance")) or len(of("embodied_response_interaction_started")),
        "plans_created": len(of("plan_created")) or len(of("plan_accepted")),
        "plans_suspended": len(of("plan_suspended")),
        "plans_resumed": len(of("plan_resumed")),
        "plans_completed": len(of("plan_completed")),
        "plans_cleared_other": "",
        "releases": "",
        "watering_interrupted": 0,
        "watering_completed": 0,
        "resumed_same_plant": "",
        "orders_started": len(of("order_started")),
        "orders_served": len(of("order_served")),
        "order_latency_s": "",
        "utterances_while_order_pending": "",
        "stale_replies": sum(1 for e in of("interactive_reply_received") if payload(e).get("stale_guide_reply")),
        "stale_replies_replaced": sum(1 for e in of("interactive_reply_received") if payload(e).get("stale_reply_replaced")),
    }

    times = [t for t in (parse_time(e) for e in events) if t is not None]
    if len(times) >= 2:
        row["duration_s"] = round(max(times) - min(times), 1)

    cleared = [payload(e).get("reason", "") for e in of("plan_cleared")]
    row["plans_cleared_other"] = ";".join(sorted({r for r in cleared if r != "completed"}))
    row["releases"] = ";".join(payload(e).get("source", "") for e in of("suspended_plan_released"))

    # Watering: was the plant that got interrupted the one she later finished?
    interrupted_plants, same, different, abandoned = [], 0, 0, 0
    bound_reason = {}
    for event in events:
        kind, data = event.get("event_type"), payload(event)
        if kind == "watering_bound":
            bound_reason[data.get("plant_id")] = data.get("why", "")
            if interrupted_plants:
                if data.get("plant_id") == interrupted_plants[-1]:
                    same += 1
                else:
                    different += 1
                interrupted_plants.pop()
        elif kind == "plan_cleared" and data.get("reason", "") != "completed" and interrupted_plants:
            abandoned += len(interrupted_plants)
            interrupted_plants.clear()
        elif kind == "watering_ended":
            if data.get("completed"):
                row["watering_completed"] += 1
            else:
                row["watering_interrupted"] += 1
                if data.get("unfinished_recorded"):
                    interrupted_plants.append(data.get("plant_id"))
    if same or different or abandoned or interrupted_plants:
        row["resumed_same_plant"] = (
            f"same={same} different={different} abandoned={abandoned} never_resumed={len(interrupted_plants)}")

    # Orders: latency from order_started to the next order_served, and how often the player had to speak meanwhile.
    latencies, repeats = [], []
    pending_start = None
    pending_utterances = 0
    for event in events:
        kind = event.get("event_type")
        if kind == "order_started":
            if pending_start is None:
                pending_start = parse_time(event)
                pending_utterances = 0
        elif kind == "player_utterance" and pending_start is not None:
            pending_utterances += 1
        elif kind == "order_served" and pending_start is not None:
            served = parse_time(event)
            if served is not None:
                latencies.append(round(served - pending_start, 1))
            repeats.append(pending_utterances)
            pending_start = None
    if pending_start is not None:
        latencies.append("never_served")
        repeats.append(pending_utterances)
    row["order_latency_s"] = ";".join(str(v) for v in latencies)
    row["utterances_while_order_pending"] = ";".join(str(v) for v in repeats)
    return row


def main(argv):
    csv_path = None
    if "--csv" in argv:
        index = argv.index("--csv")
        csv_path = argv[index + 1]
        del argv[index:index + 2]
    target = argv[0] if argv else os.path.join(os.path.dirname(__file__), "..", "Saved", "ResearchLogs")
    files = sorted(glob.glob(os.path.join(target, "*.jsonl"))) if os.path.isdir(target) else [target]

    rows = []
    for path in files:
        events = load(path)
        if not events:
            continue
        for trial_id, condition, trial_events in split_trials(events):
            rows.append(summarise(path, trial_id, condition, trial_events))

    if not rows:
        print("no events found")
        return 1
    columns = list(rows[0].keys())
    if csv_path:
        with open(csv_path, "w", encoding="utf-8-sig", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)
        print(f"wrote {len(rows)} rows to {csv_path}")
    else:
        for row in rows:
            print("-" * 60)
            for column in columns:
                if row[column] not in ("", 0):
                    print(f"{column:32s} {row[column]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
