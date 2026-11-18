"""Exercise the shipped stdio MCP bridge against an isolated WARP process."""

import base64
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time


BRIDGE = Path(__file__).with_name("dx11-mcp.py")


class Mcp:
    def __init__(self, process):
        self.process = process
        self.id = 0

    def request(self, method, **params):
        self.id += 1
        message = {"jsonrpc": "2.0", "id": self.id, "method": method, "params": params}
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        response = json.loads(self.process.stdout.readline())
        assert response["id"] == self.id, response
        return response["result"]

    def tool(self, tool_name, **args):
        result = self.request("tools/call", name="dx11_" + tool_name, arguments=args)
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise AssertionError(f"{tool_name}: {text}")
        return json.loads(text)

    def wait_capture(self, capture_id):
        for _ in range(100):
            result = self.tool("capture_status", capture_id=capture_id)
            if result["state"] != "pending":
                assert result["state"] == "ready", result
                return result
        raise AssertionError("capture did not finish")

    def bytes(self, capture_id):
        return base64.b64decode(self.tool("capture_bytes", capture_id=capture_id)["bytes_base64"])


def verify(mcp, pid, folder):
    assert mcp.request("initialize", protocolVersion="2025-06-18")["protocolVersion"] == "2025-06-18"
    assert len(mcp.request("tools/list")["tools"]) >= 15
    deadline = time.monotonic() + 8
    while pid not in mcp.tool("discover")["pids"]:
        assert time.monotonic() < deadline, "proxy endpoint never appeared"
        time.sleep(0.1)
    assert mcp.tool("status")["pid"] == pid
    bindings = mcp.tool("hotkeys")["bindings"] + mcp.tool("hotkeys", offset=48)["bindings"]
    assert any(row["id"] == "Hunting.next_indexbuffer" for row in bindings), bindings
    assert any(row["id"].lower() == "keymcpfixture.key" for row in bindings), bindings
    assert mcp.tool("hotkey", action="KeyMcpFixture.key")["dispatched"]
    assert mcp.tool("hotkey", action="Hunting.next_indexbuffer")["dispatched"]
    hunt = mcp.tool("hunting", kind="ib")
    assert hunt["selected"] in hunt["hashes"], hunt
    inspect = mcp.tool("capture", kind="ib")
    full_hash = mcp.wait_capture(inspect["id"])["capture"]["buffer"]["resource_hash"]
    assert full_hash in hunt["hashes"], hunt
    live = mcp.tool("capture", kind="ib", hash=full_hash, hash_mode="resource")
    result = mcp.wait_capture(live["id"])
    assert result["capture"]["buffer"]["resource_hash"] == full_hash, result
    assert struct.unpack("<IIII", mcp.bytes(live["id"]))[1:] == (1, 2, 3)
    exact = mcp.tool("capture", kind="ib", hash=full_hash, indexed_range=True)
    capture = mcp.wait_capture(exact["id"])["capture"]
    assert capture["type"] == 2 and capture["draw"] == 2, capture
    assert capture["index_range"] == {"offset": 4, "length": 12, "index_width": 4}, capture
    assert struct.unpack("<III", mcp.bytes(exact["id"])) == (1, 2, 3)
    instanced = mcp.tool("capture", kind="ib", draw=3, indexed_range=True)
    capture = mcp.wait_capture(instanced["id"])["capture"]
    assert capture["type"] == 4 and capture["first_index"] == 1, capture
    assert capture["index_range"] == {"offset": 8, "length": 8, "index_width": 4}, capture
    assert struct.unpack("<II", mcp.bytes(instanced["id"])) == (2, 3)
    assert mcp.tool("status")["region_enabled"]
    region_hash = next(value for value in mcp.tool("hunting", kind="ib")["hashes"] if value != full_hash)
    region = mcp.tool("capture", kind="ib", hash=region_hash, hash_mode="region", offset=4, length=12)
    result = mcp.wait_capture(region["id"])
    assert result["capture"]["matched_by"] == "hunting_region", result
    assert struct.unpack("<III", mcp.bytes(region["id"])) == (1, 2, 3)
    miss = mcp.tool("capture", kind="ib", hash="12345678", hash_mode="resource", frames=2)
    for _ in range(20):
        state = mcp.tool("capture_status", capture_id=miss["id"])["state"]
        if state != "pending":
            break
    assert state == "no_matching_draw", state
    mod_draw = mcp.tool("capture", kind="cb", stage="cs", slot=0, source="mod")
    result = mcp.wait_capture(mod_draw["id"])
    assert result["capture"]["injected"] and result["capture"]["buffer"]["stage"] == "cs", result
    assert struct.unpack("<IIII", mcp.bytes(mod_draw["id"])) == (11, 22, 33, 44)
    output = mcp.tool("capture", kind="uav", stage="cs", slot=0, source="mod")
    mcp.wait_capture(output["id"])
    assert struct.unpack("<4I", mcp.bytes(output["id"])) == (99, 0, 0, 0)
    assert "resourcemcpfixture" in [name.lower() for name in mcp.tool("custom_buffers")["resources"]]
    custom = mcp.tool("custom_buffer", name="ResourceMcpFixture")
    assert custom["state"] == "ready", custom
    assert struct.unpack("<IIII", mcp.bytes(custom["id"])) == (11, 22, 33, 44)
    for name, code in (
        ("ResourceMcpMissing", "CUSTOM_RESOURCE_NOT_FOUND"),
        ("ResourceMcpPending", "CUSTOM_RESOURCE_UNINITIALIZED"),
        ("ResourceMcpCleared", "CUSTOM_RESOURCE_NULL"),
        ("ResourceMcpTexture", "CUSTOM_RESOURCE_NOT_BUFFER"),
    ):
        error = mcp.request("tools/call", name="dx11_custom_buffer", arguments={"name": name})
        assert error["isError"] and error["structuredContent"]["code"] == code, (name, error)
    shader = next(row for row in mcp.tool("shaders")["shaders"] if row["name"].lower() == "customshadermcpfixture")
    assert shader["stage"] == "cs", shader
    bytecode = mcp.tool("shader", shader_id=shader["id"])
    assert mcp.bytes(bytecode["id"]).startswith(b"DXBC")
    for _ in range(3):
        mcp.tool("hotkey", action="ctrl+f9")
    deadline = time.monotonic() + 3
    while True:
        profile = mcp.tool("profiling", length=8192)
        if "Top Commands" in profile["text"] and profile["total_bytes"] > 65536:
            break
        assert time.monotonic() < deadline, profile["total_bytes"]
        time.sleep(0.05)
    chunks = [profile["text"]]
    while profile["next_offset"] < profile["total_bytes"]:
        profile = mcp.tool("profiling", snapshot_id=profile["snapshot_id"], offset=profile["next_offset"], length=8192)
        chunks.append(profile["text"])
    combined = "".join(chunks)
    whole = mcp.tool("profiling", snapshot_id=profile["snapshot_id"])
    assert whole["text"] == combined and len(combined.encode("utf-8")) == whole["total_bytes"]
    fresh = mcp.tool("profiling", length=8192)
    assert fresh["snapshot_id"] != whole["snapshot_id"]
    expired = mcp.request("tools/call", name="dx11_profiling",
                          arguments={"snapshot_id":whole["snapshot_id"], "length":8192})
    assert expired["structuredContent"]["code"] == "PROFILE_SNAPSHOT_EXPIRED", expired
    mcp.tool("hotkey", action="f8")
    deadline = time.monotonic() + 3
    while True:
        state = mcp.tool("status")
        if state["frame_analysis_dir"] and not state["capture_active"]:
            break
        assert time.monotonic() < deadline, state
    files = mcp.tool("frame_analysis_files")["files"]
    assert "log.txt" in files and any(".buf" in name for name in files), files
    assert mcp.tool("frame_analysis_read", name="log.txt")["total_bytes"] > 0
    blocked = mcp.request("tools/call", name="dx11_hotkey", arguments={"action": "ctrl+alt+f10"})
    assert blocked.get("isError"), blocked
    with (folder / "d3dx.ini").open("a", encoding="ascii") as ini:
        ini.write("\n[KeyMcpReloaded]\nkey=VK_F11\n")
    assert mcp.tool("hotkey", action="f10")["dispatched"]
    bindings = mcp.tool("hotkeys", offset=48)["bindings"]
    assert any(row["id"].lower() == "keymcpreloaded.key" for row in bindings), bindings


def main(harness, dll):
    assert "build" in harness.parent.parts, "keep WARP fixture inside the build directory"
    with tempfile.TemporaryDirectory(prefix="mcp-warp-", dir=harness.parent) as directory:
        folder = Path(directory)
        copy = folder / "d3d11.dll"
        shutil.copyfile(dll, copy)
        lists = [f"CommandListMcpProfilingLongNameForPagedSnapshots{index:04d}" for index in range(700)]
        profile_runs = "".join(f"run={name}\n" for name in lists)
        profile_lists = "".join(f"[{name}]\n$mcp_profile_tick = $mcp_profile_tick + 1\n" for name in lists)
        (folder / "d3dx.ini").write_text(
            "[Logging]\ncalls=1\n[System]\nmcp_enabled=1\nallow_platform_update=1\n"
            "[Rendering]\ntrack_region_hashes=1\n"
            "[Hunting]\nhunting=1\nanalyse_frame=VK_F8\nanalyse_options=dump_ib buf\nmonitor_performance_interval=0.01\n"
            "[KeyMcpFixture]\nkey=VK_F12\n"
            "[Constants]\nglobal $mcp_profile_tick = 0\n"
            "[ResourceMcpPending]\ntype=Buffer\nformat=R32_UINT\nbyte_width=16\n"
            "[ResourceMcpCleared]\n"
            "[ResourceMcpTexture]\ntype=Texture2D\nwidth=1\nheight=1\narray=1\nmips=1\nmsaa=1\n"
            "format=R8G8B8A8_UNORM\nbind_flags=shader_resource\n"
            "[ResourceMcpFixture]\ntype=Buffer\nbind_flags=constant_buffer\nformat=R32_UINT\ndata=11 22 33 44\n"
            "[ResourceMcpOutput]\ntype=Buffer\nbind_flags=unordered_access\nformat=R32_UINT\ndata=0 0 0 0\n"
            "[CustomShaderMcpFixture]\ncs=mcp.hlsl\ncs-cb0=ResourceMcpFixture\n"
            "cs-u0=ResourceMcpOutput\ncs-t0=ResourceMcpTexture\ndispatch=1, 1, 1\n"
            "[Present]\nrun=CustomShaderMcpFixture\nResourceMcpCleared=ref ResourceMcpFixture\n"
            "ResourceMcpCleared=null\n" + profile_runs + profile_lists, encoding="ascii")
        (folder / "mcp.hlsl").write_text("RWBuffer<uint> output : register(u0); "
                                        "[numthreads(1,1,1)] void main() { output[0] = 99; }", encoding="ascii")
        with subprocess.Popen([str(harness), "--mcp-live", str(copy)], cwd=folder) as game:
            try:
                with subprocess.Popen([sys.executable, str(BRIDGE), "--pid", str(game.pid)], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, encoding="utf-8") as server:
                    try:
                        verify(Mcp(server), game.pid, folder)
                    finally:
                        server.stdin.close()
                        server.wait(timeout=5)
            finally:
                (folder / "stop").touch()
                try:
                    game.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    game.kill()
                    game.wait()
                if game.returncode:
                    log = folder / "d3d11_log.txt"
                    if log.exists():
                        print("\n".join(log.read_text(encoding="utf-8", errors="replace").splitlines()[-30:]),
                              file=sys.stderr)
            assert game.returncode == 0, game.returncode
    print("PASS: DX11 MCP stdio, hotkeys, Hunting hashes, dynamic IB, custom resources/shaders, profiling, F8")


def verify_monitor(mcp, harness, dll, folder):
    start = mcp.tool("monitor_start", target_exe=harness.name, stall_ms=450)
    assert start["state"] == "monitoring", start
    assert mcp.tool("monitor_poll")["waiting_for_dll"]
    copied = folder / "d3d11.dll"
    shutil.copyfile(dll, copied)
    (folder / "d3dx.ini").write_text(
        "[Logging]\nlog_level=disabled\n[System]\nallow_platform_update=1\nmcp_enabled=0\n"
        "[Hunting]\nhunting=2\n", encoding="ascii")
    cursor = start["next_seq"]

    def until(pid, condition):
        nonlocal cursor
        deadline = time.monotonic() + 8
        records = []
        while time.monotonic() < deadline:
            result = mcp.tool("monitor_poll", after_seq=cursor, limit=256, wait_ms=150, pid=pid)
            cursor = result["next_seq"]
            records.extend(result["events"])
            if condition(result, records):
                return records
        raise AssertionError((result["sessions"], [(r["kind"], r["message"]) for r in records[-20:]]))

    with subprocess.Popen([str(harness), "--mcp-pre-present-crash", str(copied)], cwd=folder) as game:
        try:
            data = until(game.pid, lambda reply, _: any(s["state"] == "no_progress" and
                         s["phase"] == "d3d11_device_ready" for s in reply["sessions"]))
            assert any(item["kind"] == "attach" and item["detail"] == 0 for item in data), data[:5]
            assert any(item["kind"] == "info" and "successfully initialized" in item["message"] for item in data)
            assert not (folder / "d3d11_log.txt").exists()
            assert game.pid not in mcp.tool("discover")["pids"]
            (folder / "crash").touch()
            game.wait(timeout=5)
            data = until(game.pid, lambda _, records: any(item["kind"] == "process_exit" for item in records))
            assert next(item for item in data if item["kind"] == "process_exit")["exit_code"] == 0xC0000005
        finally:
            if game.poll() is None:
                game.kill()
                game.wait()
        retained = mcp.tool("monitor_poll", pid=game.pid, after_seq=0, limit=256)
        assert retained["sessions"][0]["state"] == "exited" and retained["events"][0]["kind"] == "attach"

    with subprocess.Popen([str(harness), "--mcp-instant-crash", str(copied)], cwd=folder) as game:
        try:
            game.wait(timeout=5)
            data = until(game.pid, lambda _, records: any(item["kind"] == "process_exit" for item in records))
            assert game.returncode == 0xC0000005
            assert any(item["kind"] == "attach" and item["detail"] == 0 for item in data)
            assert any(item["kind"] == "phase" and item["message"] == "dll_attached" for item in data)
            assert not any(item["kind"] == "present" for item in data)
        finally:
            if game.poll() is None:
                game.kill()
                game.wait()

    with subprocess.Popen([str(harness), "--mcp-live", str(copied)], cwd=folder) as game:
        try:
            data = until(game.pid, lambda reply, _: any(s["state"] == "presenting" for s in reply["sessions"]))
            present = next(item for item in data if item["kind"] == "present")
            # Exercise recovery from a real producer event with its original attach omitted.
            from dx11_mcp_monitor import Monitor, Transport
            reader = Monitor()
            reader.transport = Transport()
            reader.target_exe = harness.name.casefold()
            try:
                reader.accept(dict(present))
                recovered, = reader.statuses(game.pid)
                assert recovered["state"] == "presenting" and recovered["identity_source"] == "process_image", recovered
            finally:
                for entry in reader.sessions.values():
                    if entry["handle"]:
                        reader.transport.api.CloseHandle(entry["handle"])
        finally:
            (folder / "stop").touch()
            try:
                game.wait(timeout=5)
            except subprocess.TimeoutExpired:
                game.kill()
                game.wait()
        data = until(game.pid, lambda _, records: any(item["kind"] == "process_exit" for item in records))
        assert next(item for item in data if item["kind"] == "process_exit")["exit_code"] == 0

    assert mcp.tool("monitor_stop")["state"] == "stopped"
    assert mcp.tool("monitor_poll", after_seq=cursor)["state"] == "stopped"


def monitor_main(harness, dll):
    assert "build" in harness.parent.parts
    with tempfile.TemporaryDirectory(prefix="mcp-monitor-", dir=harness.parent) as directory:
        with subprocess.Popen([sys.executable, str(BRIDGE)], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, encoding="utf-8") as server:
            try:
                verify_monitor(Mcp(server), harness, dll, Path(directory))
            finally:
                server.stdin.close()
                server.wait(timeout=5)
    print("PASS: monitored startup without Present or a log file, loading stall, process crash, retained attach logs")


if __name__ == "__main__":
    runner, args = (monitor_main, sys.argv[2:]) if sys.argv[1] == "--monitor" else (main, sys.argv[1:])
    runner(*(Path(arg).resolve() for arg in args))
