"""Compare Unity enqueue records with CPU upload events exported from DX11 MCP."""

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re


UPLOAD_APIS = {"CreateBuffer", "UpdateSubresource", "UpdateSubresource1"}


def valid_crc(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]{8}", value) is not None


def read_producers(path):
    seen = set()
    with path.open(encoding="utf-8-sig") as file:
        for line_number, line in enumerate(file, 1):
            if not line.strip():
                continue
            row = json.loads(line)
            if row.get("kind") != "palette_enqueue":
                continue
            enqueue_id = row["enqueueId"]
            if not enqueue_id or enqueue_id in seen:
                raise ValueError(f"Missing or repeated enqueue ID on producer line {line_number}")
            seen.add(enqueue_id)
            if not (0 < row["beforeEnqueue"] <= row["afterEnqueue"]):
                raise ValueError(f"Invalid enqueue timestamps on producer line {line_number}")
            if row["destinationByteOffset"] < 0 or row["byteCount"] <= 0 or not valid_crc(row["crc32c"]):
                raise ValueError(f"Invalid upload fingerprint on producer line {line_number}")
            yield row


def event_key(event):
    # A deferred write can have several submissions for the same recorded call.
    return event["record"], event["submit_qpc"], event.get("execute_id", 0)


def index_uploads(bundle, windows):
    by_payload, by_id = defaultdict(list), defaultdict(list)
    seen, observed, fingerprints, tags = {}, {}, {}, {}
    for event in bundle["events"]:
        if event.get("target_id", bundle["target_id"]) != bundle["target_id"]:
            raise ValueError("Consumer events must belong to the declared target")
        if event.get("api") not in UPLOAD_APIS:
            continue
        key = event_key(event)
        call = (event["record_thread"], event["record_qpc"])
        record = event["record"]
        if record in observed and observed[record] != call:
            raise ValueError(f"Conflicting recording thread or time for consumer record {record}")
        observed[record] = call
        enqueue_id = event.get("producer_enqueue_id")
        if enqueue_id and tags.setdefault(record, enqueue_id) != enqueue_id:
            raise ValueError(f"Conflicting enqueue IDs for consumer record {record}")
        offset_range = event.get("destination_range")
        crc = event.get("cpu_crc32c")
        if (not offset_range or not valid_crc(crc) or not event.get("resource_id") or
                offset_range["bytes"] != event.get("cpu_source_bytes") or offset_range["bytes"] <= 0):
            continue
        payload = (offset_range["offset"], offset_range["bytes"], crc.lower())
        evidence = (event["resource_id"], payload)
        if fingerprints.setdefault(record, evidence) != evidence:
            raise ValueError(f"Conflicting upload fingerprints for consumer record {record}")
        seen.setdefault(key, (payload, event))

    # These windows come from the app-owned consumer calling the DX11 API.
    # Plugin marker intervals cannot claim an enqueue ID at this boundary.
    window_ids = set()
    for window in windows:
        enqueue_id = window["enqueueId"]
        if (window.get("kind") != "dx11_upload_call" or window.get("targetId") != bundle["target_id"] or
                window.get("timestampClock") != "windows_qpc" or window["timestampFrequency"] != bundle["qpc_frequency"] or
                not enqueue_id or enqueue_id in window_ids or window["nativeThreadId"] <= 0 or
                not 0 < window["beforeUpload"] <= window["afterUpload"]):
            raise ValueError(f"Invalid or duplicate DX11 call window for enqueue ID {enqueue_id}")
        window_ids.add(enqueue_id)
        matches = [
            record for record, (thread, ticks) in observed.items()
            if thread == window["nativeThreadId"] and window["beforeUpload"] <= ticks <= window["afterUpload"]
        ]
        if len(matches) > 1:
            raise ValueError(f"More than one DX11 upload falls inside call window {enqueue_id}")
        if any(tag == enqueue_id and record not in matches for record, tag in tags.items()):
            raise ValueError(f"Existing consumer tag falls outside call window {enqueue_id}")
        if matches and tags.setdefault(matches[0], enqueue_id) != enqueue_id:
            raise ValueError(f"Conflicting enqueue IDs for consumer record {matches[0]}")

    for payload, event in seen.values():
        enqueue_id = tags.get(event["record"])
        if enqueue_id:
            by_id[enqueue_id].append((payload, event))
        else:
            by_payload[payload].append(event)
    return by_payload, by_id, window_ids


def match_uploads(producers, bundle, window_ms, windows=()):
    if not bundle.get("target_id") or bundle["qpc_frequency"] <= 0 or window_ms < 0:
        raise ValueError("Supply a consumer target, its QPC frequency and a nonnegative queue window")
    window_ticks = window_ms * bundle["qpc_frequency"] // 1000
    by_payload, by_id, window_ids = index_uploads(bundle, windows)
    candidates = []
    for row in producers:
        state = "no_candidate"
        events = []
        if row.get("targetId") != bundle["target_id"]:
            state = "target_mismatch"
        elif row.get("timestampClock") != "windows_qpc" or row["timestampFrequency"] != bundle["qpc_frequency"]:
            state = "clock_mismatch"
        else:
            payload = (row["destinationByteOffset"], row["byteCount"], row["crc32c"].lower())
            tagged = by_id.get(row["enqueueId"], ())
            if tagged:
                events = [event for _, event in tagged]
                state = "id_conflict"
                if (len({event["record"] for event in events}) == 1 and tagged[0][0] == payload and
                        events[0]["record_qpc"] >= row["beforeEnqueue"]):
                    state = "matched_by_id"
            elif row["enqueueId"] in window_ids:
                state = "direct_upload_not_observed"
            else:
                bound = row["afterEnqueue"] + window_ticks
                events = [event for event in by_payload.get(payload, ())
                          if row["beforeEnqueue"] <= event["record_qpc"] <= bound]
                if events:
                    state = "candidate"
        candidates.append((row, state, events))

    competing = Counter(record for _, state, events in candidates if state == "candidate"
                        for record in {event["record"] for event in events})
    for row, state, events in candidates:
        if state == "candidate":
            records = {event["record"] for event in events}
            if len(records) != 1 or competing[events[0]["record"]] > 1:
                state = "ambiguous"
        yield {
            "enqueue_id": row["enqueueId"],
            "appearance_observation_id": row.get("assemblyObservationId"),
            "renderer_instance_id": row.get("rendererInstanceId"),
            "state": state,
            "uploads": [
                {key: event.get(key) for key in
                 ("api", "resource_id", "record", "record_thread", "record_qpc", "submit_qpc", "submission", "execute_id")}
                for event in sorted(events, key=event_key)
            ],
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("producer_jsonl", type=Path)
    parser.add_argument("consumer_json", type=Path, help="Object with target_id, qpc_frequency and events from DX11 MCP")
    parser.add_argument("--upload-windows", type=Path, help="JSONL emitted around exact DX11 calls by the app-owned consumer")
    parser.add_argument("--window-ms", required=True, type=int, help="Measured maximum queue delay after producer return")
    args = parser.parse_args()
    try:
        bundle = json.loads(args.consumer_json.read_text(encoding="utf-8-sig"))
        windows = ()
        if args.upload_windows:
            windows = [json.loads(line) for line in args.upload_windows.read_text(encoding="utf-8-sig").splitlines()
                       if line.strip()]
        results = list(match_uploads(read_producers(args.producer_jsonl), bundle, args.window_ms, windows))
    except (OSError, KeyError, TypeError, ValueError) as error:
        parser.exit(2, f"{parser.prog}: {error}\n")
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
