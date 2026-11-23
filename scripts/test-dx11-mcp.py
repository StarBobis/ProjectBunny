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

    def send(self, method, **params):
        self.id += 1
        message = {"jsonrpc": "2.0", "id": self.id, "method": method, "params": params}
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        return self.id

    def receive(self):
        return json.loads(self.process.stdout.readline())

    def request(self, method, **params):
        request_id = self.send(method, **params)
        response = self.receive()
        assert response["id"] == request_id, response
        return response["result"]

    def tool(self, tool_name, **args):
        result = self.request("tools/call", name="dx11_" + tool_name, arguments=args)
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise AssertionError(f"{tool_name}: {text}")
        return json.loads(text)

    def wait_capture(self, capture_id, expected="ready"):
        for _ in range(100):
            result = self.tool("capture_status", capture_id=capture_id)
            if result["state"] != "pending":
                assert expected is None or result["state"] == expected, result
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
    armed = mcp.tool("texture_dhash", ib_hash=full_hash, index_count=3, slots=[16, 17, 18])
    textures = mcp.wait_capture(armed["id"])["capture"]
    assert textures["phase"] == "original_before_overrides" and textures["first_index"] == 0, textures
    assert textures["ib_resource_hash"] == full_hash and textures["ps_hash"], textures
    slots = textures["slots"]
    assert [entry["slot"] for entry in slots] == [16, 17, 18], slots
    assert slots[0]["dhash"] == "ffff00000000ffff" and slots[0]["width"] == 4, slots
    assert slots[1]["state"] == "unbound" and slots[2]["dhash"] == "00000000ffffffff", slots
    other = mcp.tool("texture_dhash", ib_hash=full_hash, index_count=2, first_index=1,
                     ps_hash=textures["ps_hash"], slots=[16])
    assert mcp.wait_capture(other["id"])["capture"]["slots"][0]["dhash"] == "00000000ffffffff"
    assert mcp.tool("status")["region_enabled"]
    region_hash = next(value for value in mcp.tool("hunting", kind="ib")["hashes"] if value != full_hash)
    region = mcp.tool("capture", kind="ib", hash=region_hash, hash_mode="region", offset=4, length=12)
    result = mcp.wait_capture(region["id"])
    assert result["capture"]["matched_by"] == "hunting_region", result
    assert struct.unpack("<III", mcp.bytes(region["id"])) == (1, 2, 3)
    indexed_draw = result["capture"]
    armed = mcp.tool("texture_dhash", ib_hash=region_hash, hash_mode="region", resource_hash=full_hash,
                     index_count=indexed_draw["index_count"], first_index=indexed_draw["first_index"],
                     ps_hash=textures["ps_hash"], slots=[16])
    captured = mcp.wait_capture(armed["id"])["capture"]
    expected = "ffff00000000ffff" if indexed_draw["first_index"] == 0 else "00000000ffffffff"
    assert captured["ib_hash"] == region_hash and captured["slots"][0]["dhash"] == expected, captured
    (folder / "update-red-texture").touch()
    wait_file(folder / "texture-update-observed")
    refreshed = mcp.tool("texture_dhash", ib_hash=full_hash, index_count=3, slots=[16])
    updated_texture = mcp.wait_capture(refreshed["id"])["capture"]["slots"][0]

    cleared = folder / "clear-ps-state"
    cleared.touch()
    try:
        stale = mcp.tool("texture_dhash", ib_hash=full_hash, index_count=3, slots=[16],
                         ps_hash=textures["ps_hash"], frames=2)
        stale_shader = mcp.wait_capture(stale["id"], expected=None)
        unfiltered = mcp.tool("texture_dhash", ib_hash=full_hash, index_count=3, slots=[16])
        draw_without_ps = mcp.wait_capture(unfiltered["id"])["capture"]
    finally:
        cleared.unlink()
    assert (updated_texture.get("dhash"), stale_shader["state"], draw_without_ps["ps_hash"]) == (
        "00000000ffffffff", "no_matching_draw", None), (updated_texture, stale_shader, draw_without_ps)

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


def verify_device_switch(mcp, pid, folder):
    deadline = time.monotonic() + 8
    while pid not in mcp.tool("discover")["pids"]:
        assert time.monotonic() < deadline
        time.sleep(0.1)
    assert mcp.tool("status")["pid"] == pid
    pending = mcp.tool("capture", kind="ib", hash="12345678", hash_mode="resource", frames=120)
    assert pending["state"] == "pending"
    (folder / "switch-device").touch()
    wait_file(folder / "replacement-presenting")
    assert mcp.tool("status")["pid"] == pid
    retired = mcp.tool("capture_status", capture_id=pending["id"])
    assert retired["state"] == "device_changed", retired
    replacement = mcp.tool("capture", kind="ib", indexed_range=True)
    mcp.wait_capture(replacement["id"])
    assert struct.unpack("<III", mcp.bytes(replacement["id"])) == (1, 2, 3)


def wait_file(path, timeout=8):
    deadline = time.monotonic() + timeout
    while not path.exists():
        assert time.monotonic() < deadline, path.name
        time.sleep(0.05)


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
    with tempfile.TemporaryDirectory(prefix="mcp-device-switch-", dir=harness.parent) as directory:
        folder = Path(directory)
        copy = folder / "d3d11.dll"
        shutil.copyfile(dll, copy)
        (folder / "d3dx.ini").write_text(
            "[Logging]\ncalls=1\n[System]\nmcp_enabled=1\nallow_platform_update=1\n"
            "[Hunting]\nhunting=2\n", encoding="ascii")
        with subprocess.Popen([str(harness), "--mcp-live", str(copy)], cwd=folder) as game:
            try:
                with subprocess.Popen([sys.executable, str(BRIDGE), "--pid", str(game.pid)],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE, encoding="utf-8") as server:
                    try:
                        verify_device_switch(Mcp(server), game.pid, folder)
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
            assert game.returncode == 0
    print("PASS: DX11 MCP stdio, hotkeys, Hunting, readback, profiling, F8, and replacement device handoff")


def verify_monitor(mcp, harness, dll, folder):
    start = mcp.tool("monitor_start", target_exe=harness.name, stall_ms=450)
    assert start["state"] == "monitoring", start
    assert start["stall_ms"] == 450 and start["present_stall_ms"] == 2000, start
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
            deadline = time.monotonic() + 2.3
            frames = set()
            while time.monotonic() < deadline:
                snapshot = mcp.tool("monitor_poll", pid=game.pid, after_seq=cursor, wait_ms=100)
                cursor = snapshot["next_seq"]
                current, = snapshot["sessions"]
                assert current["state"] == "presenting" and current["stall_threshold_ms"] == 2000, current
                frames.add(current["last_frame"])
            assert len(frames) >= 2, frames
            (folder / "pause-present").touch()
            until(game.pid, lambda reply, _: any(s["state"] == "no_progress" and
                  s["stall_threshold_ms"] == 2000 for s in reply["sessions"]))
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


def verify_map_reload(harness, dll, initial_enabled):
    with tempfile.TemporaryDirectory(prefix="mcp-map-reload-", dir=harness.parent) as directory:
        folder = Path(directory)
        copy = folder / "d3d11.dll"
        shutil.copyfile(dll, copy)
        ini = folder / "d3dx.ini"

        def configure(enabled):
            ini.write_text(
                "[Logging]\nlog_level=disabled\n[System]\nallow_platform_update=1\n"
                f"mcp_enabled={int(enabled)}\nconfig_initialization_delay=6\n"
                "[Hunting]\nhunting=2\nreload_config=F10\n"
                "[ResourceMcpMapped]\n[Present]\nResourceMcpMapped = ref ib\n", encoding="ascii")

        configure(initial_enabled)
        hold = folder / "hold-map"
        hold.touch()
        (folder / "present-only").touch()
        with subprocess.Popen([str(harness), "--mcp-live", str(copy)], cwd=folder) as game:
            try:
                with subprocess.Popen([sys.executable, str(BRIDGE), "--pid", str(game.pid)],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE, encoding="utf-8") as server:
                    try:
                        mcp = Mcp(server)

                        def wait_endpoint(available):
                            deadline = time.monotonic() + 10
                            while (game.pid in mcp.tool("discover")["pids"]) != available:
                                assert game.poll() is None and time.monotonic() < deadline, available
                                time.sleep(0.1)

                        def read_buffer():
                            deadline = time.monotonic() + 3
                            while True:
                                try:
                                    return mcp.tool("custom_buffer", name="ResourceMcpMapped")
                                except AssertionError:
                                    # The bridge can be holding the connection from before the reload.
                                    assert game.poll() is None and time.monotonic() < deadline
                                    time.sleep(0.1)

                        wait_file(folder / "map-held")
                        if initial_enabled:
                            wait_endpoint(True)
                            assert read_buffer()["state"] == "mapped"
                            configure(False)
                            assert mcp.tool("hotkey", action="Hunting.reload_config")["dispatched"]
                            wait_endpoint(False)
                            hold.unlink()
                            wait_file(folder / "map-released")
                        configure(True)
                        wait_endpoint(True)
                        if not initial_enabled:
                            assert read_buffer()["state"] == "mapped"
                            hold.unlink()
                            wait_file(folder / "map-released")
                        result = read_buffer()
                        assert result["state"] == "ready", result
                        assert struct.unpack("<4I", mcp.bytes(result["id"])) == (100, 1, 2, 3)
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
            assert game.returncode == 0


def map_reload_main(harness, dll):
    assert "build" in harness.parent.parts
    for initial_enabled in (False, True):
        verify_map_reload(harness, dll, initial_enabled)
    print("PASS: tracked initial and cleared mapping across MCP enable/disable reloads")


def verify_multi_target(mcp, games, folders, harness_name):
    pids = {game.pid for game in games}
    deadline = time.monotonic() + 10
    while True:
        discovery = mcp.tool("discover")
        if pids <= set(discovery["pids"]):
            break
        assert time.monotonic() < deadline and all(game.poll() is None for game in games), discovery
        time.sleep(0.1)
    entries = {entry["pid"]: entry for entry in discovery["targets"] if entry["pid"] in pids}
    assert set(entries) == pids and all(x["executable"].lower() == harness_name.lower() for x in entries.values()), entries
    ids = [entries[game.pid]["target_id"] for game in games]
    assert all(ids) and len(set(ids)) == 3, entries

    ambiguous = mcp.request("tools/call", name="dx11_status", arguments={})
    assert ambiguous["structuredContent"]["code"] == "TARGET_REQUIRED", ambiguous
    conflict = mcp.request("tools/call", name="dx11_status", arguments={"pid": games[1].pid, "target_id": ids[0]})
    assert conflict["structuredContent"]["code"] == "TARGET_CONFLICT", conflict
    old_start = int(ids[1].split(":")[1], 16) - 1
    stale = mcp.request("tools/call", name="dx11_status", arguments={"target_id": f"{games[1].pid}:{old_start:016x}"})
    assert stale["structuredContent"]["code"] == "TARGET_CHANGED", stale
    for game, target_id in zip(games, ids):
        state = mcp.tool("status", target_id=target_id)
        assert (state["pid"], state["target_id"]) == (game.pid, target_id), state

    pause = folders[0] / "pause-present"
    pause.touch()
    wait_file(folders[0] / "pause-observed")
    action = "KeyMcpTargetTest.key"
    try:
        blocked = mcp.send("tools/call", name="dx11_status", arguments={"target_id": ids[0]})
        other = {mcp.send("tools/call", name="dx11_hotkey", arguments={"target_id": ids[i], "action": action}): i
                 for i in (1, 2)}
        for _ in range(2):
            reply = mcp.receive()
            assert reply["id"] in other, ("Healthy targets should finish while the first Present is paused", reply)
            index = other.pop(reply["id"])
            result = json.loads(reply["result"]["content"][0]["text"])
            assert result["dispatched"] and result["pid"] == games[index].pid and result["target_id"] == ids[index], result
    finally:
        pause.unlink()
    reply = mcp.receive()
    assert reply["id"] == blocked, reply
    assert json.loads(reply["result"]["content"][0]["text"])["pid"] == games[0].pid

    def saved(index, expected, timeout=4):
        path = folders[index] / "d3dx_user.ini"
        deadline = time.monotonic() + timeout
        marker = str(expected)
        while True:
            try:
                lines = path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
            except FileNotFoundError:
                lines = []
            except PermissionError:
                # Windows can briefly deny reads during atomic replacement.
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
                continue
            if any(line.partition("=")[0].strip() == "$target_test_value"
                   and line.partition("=")[2].strip() == marker for line in lines):
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.05)

    assert saved(1, 2) and saved(2, 3)
    assert not saved(0, 1, timeout=0.15), "The paused process must not receive the other processes' actions"
    legacy = mcp.tool("hotkey", pid=games[0].pid, action=action)
    assert legacy["dispatched"] and legacy["pid"] == games[0].pid
    assert saved(0, 1)


def multi_target_main(harness, dll):
    assert "build" in harness.parent.parts
    with tempfile.TemporaryDirectory(prefix="mcp-three-processes-", dir=harness.parent) as directory:
        root, folders, games = Path(directory), [], []
        try:
            for index in range(3):
                folder = root / str(index)
                folder.mkdir()
                folders.append(folder)
                copy = folder / "d3d11.dll"
                shutil.copyfile(dll, copy)
                (folder / "d3dx.ini").write_text(
                    "[Logging]\nlog_level=disabled\n[System]\nallow_platform_update=1\nmcp_enabled=1\n"
                    "config_initialization_delay=-1\nsettings_auto_save_interval=0\n[Hunting]\nhunting=2\n"
                    "[Constants]\nglobal persist $target_test_value=0\n"
                    "[KeyMcpTargetTest]\nkey=VK_F23\ntype=activate\n"
                    f"$target_test_value={index+1}\n", encoding="ascii")
                games.append(subprocess.Popen([str(harness), "--mcp-live", str(copy)], cwd=folder))
            with subprocess.Popen([sys.executable, str(BRIDGE)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, encoding="utf-8") as server:
                try:
                    verify_multi_target(Mcp(server), games, folders, harness.name)
                finally:
                    server.stdin.close()
                    server.wait(timeout=10)
        finally:
            for folder in folders:
                (folder / "stop").touch()
            for game in games:
                try:
                    game.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    game.kill()
                    game.wait()
        assert all(game.returncode == 0 for game in games)
    print("PASS: 3 independent PID routes, explicit instance selection, and no blocking across processes")


def verify_alternating(mcp, pid, folder):
    deadline = time.monotonic() + 8
    while pid not in mcp.tool("discover")["pids"]:
        assert time.monotonic() < deadline, "Primary WARP swap chain did not publish its endpoint"
        time.sleep(0.1)
    assert mcp.tool("status")["pid"] == pid
    job = mcp.tool("capture", kind="ib", indexed_range=True, frames=60)
    assert job["state"] == "pending"
    (folder / "alternate").touch()
    wait_file(folder / "alternated")
    pending = mcp.tool("capture_status", capture_id=job["id"])
    assert pending["state"] == "pending", pending
    pause = folder / "pause-first"
    pause.touch()
    wait_file(folder / "second-only")
    pending = mcp.tool("capture_status", capture_id=job["id"])
    assert pending["state"] == "pending", pending
    (folder / "first-draw").touch()
    pause.unlink()
    result = mcp.wait_capture(job["id"])
    assert result["bytes"] == 12 and struct.unpack("<3I", mcp.bytes(job["id"])) == (11, 22, 33), result


def alternating_main(harness, dll):
    assert "build" in harness.parent.parts
    with tempfile.TemporaryDirectory(prefix="mcp-alternating-", dir=harness.parent) as directory:
        folder = Path(directory)
        copy = folder / "d3d11.dll"
        shutil.copyfile(dll, copy)
        (folder / "d3dx.ini").write_text(
            "[Logging]\nlog_level=disabled\n[System]\nallow_platform_update=1\n"
            "mcp_enabled=1\nconfig_initialization_delay=-1\n", encoding="ascii")
        with subprocess.Popen([str(harness), "--mcp-alternating", str(copy)], cwd=folder) as game:
            try:
                with subprocess.Popen([sys.executable, str(BRIDGE), "--pid", str(game.pid)],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE, encoding="utf-8") as server:
                    try:
                        verify_alternating(Mcp(server), game.pid, folder)
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
            assert game.returncode == 0
    print("PASS: capture stays on its originating context while two live WARP devices alternate Present")


if __name__ == "__main__":
    modes = {"--monitor": monitor_main, "--map-reload": map_reload_main,
             "--multi-target": multi_target_main, "--alternating": alternating_main}
    runner = modes.get(sys.argv[1], main)
    args = sys.argv[2:] if sys.argv[1] in modes else sys.argv[1:]
    runner(*(Path(arg).resolve() for arg in args))
