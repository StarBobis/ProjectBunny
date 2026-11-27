"""Collect one APMI scene's producer and DX11 consumer evidence in a single trace."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time


BRIDGE = Path(__file__).resolve().parents[1] / "dx11-mcp.py"
FULL_IBS = ("41e0edb1", "584d513c", "b4134b8c", "ff238cbf", "3f7ce4fa", "06283421")
SCENE_IBS = {
    "details": FULL_IBS,
    "world": FULL_IBS,
    "team": ("4e993b0e", "151e3804", "8fb5ff3a", "16e90b67", "3f7ce4fa", "06283421"),
}
CONFIRMED = ("queue_position_and_references", "synchronous_call")


class Bridge:
    def __init__(self, process):
        self.process = process
        self.request_id = 0

    def request(self, method, **params):
        self.request_id += 1
        request = {"jsonrpc": "2.0", "id": self.request_id, "method": method, "params": params}
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("DX11 MCP bridge exited")
        reply = json.loads(line)
        if reply.get("id") != self.request_id or "result" not in reply:
            raise RuntimeError(f"Bridge returned an invalid reply: {reply}")
        return reply["result"]

    def tool(self, name, **arguments):
        result = self.request("tools/call", name="dx11_" + name, arguments=arguments)
        payload = result["content"][0]["text"]
        if result.get("isError"):
            raise RuntimeError(f"dx11_{name}: {payload}")
        return json.loads(payload)


def palette_writes(chain, event):
    branch, submissions = set(), set()
    for node in chain["nodes"]:
        if (node["role"] == "cs_srv" and node["slot"] == 2) or node["parent"] in branch:
            branch.add(node["node"])
            if node["submission"] is not None:
                submissions.add(node["submission"])
    return [entry for submission in sorted(submissions) if (entry := event(submission))["api"]
            in ("UpdateSubresource", "UpdateSubresource1")]


def collect(mcp, scene, hashes, frames):
    target = mcp.tool("status")
    enabled = {"apmi_palette_producer", "resource_trace_ib_groups"}
    if not enabled.issubset(target["features"]):
        raise RuntimeError("The target has not loaded the APMI producer DLL")
    args = {"target_id": target["target_id"]}
    job = mcp.tool("resource_trace", ib_hashes=hashes, frames=frames, producer_frames=24, **args)
    trace_id = job["trace_id"]
    read = {**args, "trace_id": trace_id}
    deadline = time.monotonic() + 18
    while True:
        status = mcp.tool("resource_trace_status", **read)
        if status["state"] != "pending":
            break
        if time.monotonic() >= deadline:
            status = mcp.tool("resource_trace_cancel", **read)
            break

    rows = {hash_value: [] for hash_value in hashes}
    offset = 0
    while offset < status["matches"]:
        page = mcp.tool("resource_trace_list", offset=offset, **read)
        for anchor in page["matches"]:
            rows[anchor["ib_hash"]].append(anchor)
        if page["next_offset"] <= offset:
            raise RuntimeError("Trace listing did not advance")
        offset = page["next_offset"]

    events = {}

    def event(submission):
        if submission not in events:
            events[submission] = mcp.tool("resource_trace_event", submission=submission, **read)
        return events[submission]

    best = {}
    for hash_value, anchors in rows.items():
        # Repeated passes can share an IB. Inspect up to six distinct draw
        # ranges or frames, while skipping draws that duplicate the same range.
        sampled = set()
        best_score = -1
        for anchor in anchors:
            key = (anchor["frame"], anchor["ib_resource_id"], anchor["first_index"], anchor["index_count"])
            if key in sampled:
                continue
            if len(sampled) == 6:
                break
            sampled.add(key)
            chain = mcp.tool("resource_trace_chain", submission=anchor["submission"], **read)
            uploads = palette_writes(chain, event)
            states = [(upload.get("unity_producer") or {}).get("state") for upload in uploads]
            score = max((3 if state in CONFIRMED else 2 if state == "references_candidate" else 1
                         for state in states), default=0)
            if score > best_score:
                best_score = score
                best[hash_value] = {"anchor": anchor, "chain": chain, "uploads": uploads, "confirmed_producer": score == 3}
            if score == 3:
                break

    return {"scene": scene, "target_id": target["target_id"], "status": status, "ib_hashes": hashes,
            "evidence": best, "missing_ib_hashes": [value for value in hashes if value not in best],
            "unconfirmed_producer_ib_hashes": [value for value, entry in best.items() if not entry["confirmed_producer"]]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", required=True, choices=SCENE_IBS)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ib", action="append", help="Override the scene's known IB list")
    parser.add_argument("--frames", type=int, default=2)
    args = parser.parse_args()
    hashes = [value.lower().removeprefix("0x") for value in (args.ib or SCENE_IBS[args.scene])]
    if (args.pid <= 0 or not 1 <= args.frames <= 2 or not 1 <= len(hashes) <= 16 or
            len(set(hashes)) != len(hashes) or any(not re.fullmatch(r"[0-9a-f]{8}", value) for value in hashes)):
        parser.error("Use a positive PID, 1–2 frames and 1–16 distinct eight-digit IB hashes")
    if args.output.exists():
        parser.error("Choose a new output file")
    process = subprocess.Popen([sys.executable, str(BRIDGE), "--pid", str(args.pid)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, encoding="utf-8")
    try:
        mcp = Bridge(process)
        mcp.request("initialize", protocolVersion="2025-06-18")
        report = collect(mcp, args.scene, hashes, args.frames)
        with args.output.open("x", encoding="utf-8") as file:
            json.dump(report, file, ensure_ascii=False, indent=2)
            file.write("\n")
        print(f"Saved {len(report['evidence'])}/{len(hashes)} IBs to {args.output}")
        print(f"Unconfirmed producer IBs: {', '.join(report['unconfirmed_producer_ib_hashes']) or 'none'}")
        print(f"Missing IBs: {', '.join(report['missing_ib_hashes']) or 'none'}")
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        parser.exit(2, f"{parser.prog}: {error}\n")
    finally:
        process.stdin.close()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait()


if __name__ == "__main__":
    main()
