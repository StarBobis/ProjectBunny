"""Local stdio MCP bridge for the ProjectBunny DX11 proxy."""

import argparse
import base64
from concurrent.futures import Future, ThreadPoolExecutor
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import sys
from threading import Lock
import time

from dx11_mcp_monitor import LEVELS, Monitor, Transport


CHUNK = 24 * 1024
SAVE_CHUNK = 60 * 1024
ANY = (1 << 64) - 1
PIPE_PREFIX = "ProjectBunny-DX11-"


def integer(description, minimum=0, maximum=(1 << 32) - 1, default=None):
    result = {"type": "integer", "description": description, "minimum": minimum, "maximum": maximum}
    if default is not None:
        result["default"] = default
    return result


def choice(description, *values, default=None):
    result = {"type": "string", "description": description, "enum": list(values)}
    if default is not None:
        result["default"] = default
    return result


def string(description):
    return {"type": "string", "description": description, "minLength": 1, "maxLength": 1024}


class PipeError(Exception):
    def __init__(self, message, code=None):
        self.code = code
        super().__init__(f"{code}: {message}" if code else message)


class Pipe:
    def __init__(self, pid, describe_target):
        self.pid = pid
        self.describe_target = describe_target
        self.target = None
        self.handle = None
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        ptr = ctypes.c_void_p
        dword_out = ctypes.POINTER(wintypes.DWORD)
        self._bind("CreateFileW", wintypes.HANDLE, wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                   ptr, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
        self._bind("GetNamedPipeServerProcessId", wintypes.BOOL, wintypes.HANDLE, dword_out)
        self._bind("SetNamedPipeHandleState", wintypes.BOOL, wintypes.HANDLE, dword_out, ptr, ptr)
        self._bind("PeekNamedPipe", wintypes.BOOL, wintypes.HANDLE, ptr, wintypes.DWORD, ptr, dword_out, ptr)
        for name in ("ReadFile", "WriteFile"):
            self._bind(name, wintypes.BOOL, wintypes.HANDLE, ptr, wintypes.DWORD, dword_out, ptr)
        self._bind("CloseHandle", wintypes.BOOL, wintypes.HANDLE)

    def _bind(self, name, output, *inputs):
        fn = getattr(self.api, name)
        fn.argtypes = inputs
        fn.restype = output

    def close(self):
        if self.handle is not None:
            self.api.CloseHandle(self.handle)
            self.handle = None
        self.target = None

    def connect(self, target_id=None):
        name = "\\\\.\\pipe\\" + PIPE_PREFIX + str(self.pid)
        deadline = time.monotonic() + 2
        while True:
            handle = self.api.CreateFileW(name, 0xC0000000, 0, None, 3, 0, None)
            if handle != ctypes.c_void_p(-1).value:
                break
            code = ctypes.get_last_error()
            if code != 231 or time.monotonic() >= deadline:
                raise PipeError(f"Could not connect to DX11 PID {self.pid} (Win32 {code}). Check mcp_enabled and Present.")
            time.sleep(0.025)
        self.handle = handle
        peer = wintypes.DWORD()
        mode = wintypes.DWORD(2)  # PIPE_READMODE_MESSAGE
        if (not self.api.GetNamedPipeServerProcessId(handle, ctypes.byref(peer)) or peer.value != self.pid
                or not self.api.SetNamedPipeHandleState(handle, ctypes.byref(mode), None, None)):
            self.close()
            raise PipeError("The named pipe could not be verified as a ProjectBunny endpoint for this PID.")
        self.target = self.describe_target(self.pid)
        if target_id and self.target["target_id"] != target_id:
            self.close()
            raise PipeError("The target instance changed or could not be verified. Rediscover it before retrying.",
                            "TARGET_CHANGED")

    def select(self, target_id):
        # The DLL can retire an idle pipe while other targets are busy. Check
        # the connection before dispatch so no mutating command is replayed.
        available = wintypes.DWORD()
        if self.handle is not None and not self.api.PeekNamedPipe(
                self.handle, None, 0, None, ctypes.byref(available), None):
            self.close()
        if self.handle is not None and target_id and self.target["target_id"] != target_id:
            if self.describe_target(self.pid)["target_id"] != target_id:
                raise PipeError("The target instance changed or could not be verified. Rediscover it before retrying.",
                                "TARGET_CHANGED")
            self.close()
        if self.handle is None:
            self.connect(target_id)

    def request(self, *fields, timeout=10):
        parts = [str(field) for field in fields]
        if any(any(char in part for char in "\t\r\n\0") for part in parts):
            raise ValueError("Arguments cannot contain tabs, newlines or NUL.")
        data = "\t".join(parts).encode("utf-8")
        if len(data) > 4096:
            raise ValueError("Request exceeds 4096 bytes.")
        if self.handle is None:
            self.connect()
        count = wintypes.DWORD()
        request = ctypes.create_string_buffer(data)
        if not self.api.WriteFile(self.handle, request, len(data), ctypes.byref(count), None) or count.value != len(data):
            code = ctypes.get_last_error()
            self.close()
            raise PipeError(f"DX11 pipe write failed (Win32 {code}).")
        deadline = time.monotonic() + timeout
        while True:
            if not self.api.PeekNamedPipe(self.handle, None, 0, None, ctypes.byref(count), None):
                code = ctypes.get_last_error()
                self.close()
                raise PipeError(f"DX11 pipe disconnected before the result (Win32 {code}).")
            if count.value:
                break
            if time.monotonic() >= deadline:
                self.close()
                raise PipeError("DX11 did not return a result before the deadline. Check status before repeating actions.")
            time.sleep(0.005)
        response = ctypes.create_string_buffer(65536)
        if not self.api.ReadFile(self.handle, response, len(response), ctypes.byref(count), None):
            code = ctypes.get_last_error()
            self.close()
            raise PipeError(f"DX11 pipe read failed (Win32 {code}).")
        payload = response.raw[:count.value]
        if payload.startswith(b"B"):
            return payload[1:]
        result = json.loads(payload)
        if "error" in result:
            raise PipeError(result["error"], result.get("code"))
        return result


class TargetConnection:
    """One FIFO queue and one pipe for each render endpoint."""

    def __init__(self, pid, describe_target):
        self.pipe = Pipe(pid, describe_target)
        self.worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix=f"dx11-{pid}")
        self.last_request = None

    def submit(self, handler, target_id, arguments):
        self.last_request = self.worker.submit(self.invoke, handler, target_id, arguments)
        return self.last_request

    def invoke(self, handler, target_id, arguments):
        self.pipe.select(target_id)
        return {**handler(self.pipe, **arguments), "pid": self.pipe.pid, "target_id": self.pipe.target["target_id"]}

    def close(self):
        self.worker.shutdown()
        self.pipe.close()


class Bridge:
    def __init__(self, default_pid, output_dir):
        self.default_pid = default_pid
        self.output_dir = output_dir
        self.connections = {}
        self.process_query = Transport()
        self.monitor = Monitor()
        self.monitor_worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix="dx11-monitor-tools")
        self.tools = {}
        self._register()

    def select_target(self, pid, target_id):
        if target_id:
            match = re.fullmatch(r"([1-9][0-9]{0,9}):([0-9a-fA-F]{16})", target_id)
            if not match or int(match[1]) > 0xFFFFFFFF or int(match[2], 16) == 0:
                raise PipeError("Use a target_id returned by dx11_discover.", "TARGET_ID_INVALID")
            if pid is not None and pid != int(match[1]):
                raise PipeError("pid and target_id refer to different processes.", "TARGET_CONFLICT")
            return int(match[1]), target_id.lower()
        pid = pid if pid is not None else self.default_pid
        if pid is None:
            candidates = self.live_pids()
            if len(candidates) != 1:
                raise PipeError(f"Select a pid or target_id from dx11_discover. Available PIDs: {candidates}",
                                "TARGET_REQUIRED")
            pid = candidates[0]
        return pid, None

    def prune_connections(self, live):
        for pid, connection in list(self.connections.items()):
            if pid not in live and (connection.last_request is None or connection.last_request.done()):
                connection.close()
                del self.connections[pid]

    @staticmethod
    def live_pids():
        pids = []
        for name in os.listdir("\\\\.\\pipe\\"):
            suffix = name.removeprefix(PIPE_PREFIX)
            if suffix != name and suffix.isascii() and suffix.isdecimal():
                pids.append(int(suffix))
        return sorted(pids)

    def discover(self):
        pids = self.live_pids()
        self.prune_connections(pids)
        return {"pids": pids, "targets": [self.process_query.target(pid) for pid in pids]}

    def close(self):
        for connection in self.connections.values():
            connection.close()
        self.monitor_worker.shutdown()
        self.monitor.close()

    def add(self, name, description, callback, properties=None, required=(), pipe=True):
        props = dict(properties or {})
        if pipe:
            props["pid"] = integer("Game process ID. Optional when exactly one endpoint exists.", 1)
            props["target_id"] = string("Stable process instance from dx11_discover, including PID and creation time.")
        schema = {"type": "object", "properties": props, "required": list(required), "additionalProperties": False}
        self.tools["dx11_" + name] = ({"name": "dx11_" + name, "description": description, "inputSchema": schema},
                                      callback, pipe)

    def _register(self):
        selector = {
            "kind": choice("Draw metadata or a buffer binding to capture.", "draw", "ib", "vb", "cb", "srv",
                           "uav", "so", "indirect"),
            "hash": string("32-bit resource or Hunting region hash, in hex."),
            "hash_mode": choice("Interpret the hash as a resource, a Hunting region, or either.", "auto",
                                "resource", "region", default="auto"),
            "stage": choice("Shader stage or binding group.", "vs", "hs", "ds", "gs", "ps", "cs", "om", "ia", "so"),
            "slot": integer("Pipeline slot.", 0, 127),
            "draw": integer("Draw, dispatch, or injected draw ordinal in each searched frame.", 1),
            "resource_id": integer("Opaque resource ID returned by an earlier draw inspection.", 1, ANY - 1),
            "source": choice("Select game draws, mod draws or both.", "any", "game", "mod", default="any"),
            "frames": integer("Maximum Present frames of the context that accepted the capture.", 1, 120, 30),
            "phase": choice("Before the call or after it. Auto reads UAV and stream output after the call.",
                            "auto", "before", "after", default="auto"),
            "offset": integer("Byte offset within the underlying buffer.", default=0),
            "length": integer("Bytes to capture. Zero means through the end of the buffer.",
                              0, 64 * 1024 * 1024, 0),
            "indexed_range": {"type": "boolean", "default": False,
                              "description": "For IB, capture only the indices consumed by a direct DrawIndexed or "
                                             "DrawIndexedInstanced call. Omit offset and length."},
        }
        paging = {"offset": integer("Starting index.", default=0)}
        self.add("discover", "List running DX11 endpoints with their PID, executable and process instance target_id.",
                 self.discover, pipe=False)
        self.add("monitor_start", "Arm the independent live monitor before starting the game. "
                 "Enables streaming from the DLL attach entry point and retains records after exit.",
                 self.monitor.start, {"target_exe": string("Executable file name, for example ZenlessZoneZero.exe."),
                                      "level": choice("Runtime log threshold. Startup includes info.", *LEVELS,
                                                      default="warning"),
                                      "stall_ms": integer("Startup no-progress threshold. After Present the minimum is 2000 ms.",
                                                          100, 60000, 5000)},
                 pipe=False)
        self.add("monitor_poll", "Read live startup/Present progress, in-memory log events and process exit status. "
                 "Call with the previous next_seq to continue, including while the game is hung or closed.",
                 self.monitor.poll, {"after_seq": integer("Cursor returned by the previous poll.", 0, (1 << 63) - 1, 0),
                                     "limit": integer("Maximum records.", 1, 256, 128),
                                     "wait_ms": integer("Optional long poll wait by the bridge.", 0, 30000, 0),
                                     "pid": integer("Restrict results to a process ID.", 1)}, pipe=False)
        self.add("monitor_stop", "Stop monitoring and retain the observed events for later polls.",
                 self.monitor.stop, pipe=False)
        self.add("status", "Read live DX11 frame, Hunting and F8 capture state.", lambda p: p.request("STATUS"))
        self.add("hotkeys", "List the active INI action IDs and their configured key bindings, including mod keys.",
                 lambda p, offset=0: p.request("KEYS", offset), paging)
        self.add("hotkey", "Invoke a registered action by ID. Aliases include F10, F8, Ctrl+F9 and Ctrl+Alt+F10.",
                 self.hotkey, {"action": string("INI action ID or supported shortcut alias."),
                               "phase": choice("Emit a press, release, or both. F8 hold uses explicit press/release.",
                                               "tap", "press", "release", default="tap"),
                               "confirm_wipe": {"type": "boolean", "default": False,
                                                "description": "Confirm deletion of persistent user settings."}},
                 ("action",))
        self.add("hunting", "Read selected and visited hashes from the same collections as the Hunting HUD.",
                 lambda p, kind, offset=0: p.request("HUNT", kind, offset),
                 {"kind": choice("Hunting collection.", "ib", "vb", "vs", "hs", "ds", "gs", "ps", "cs", "rt"), **paging},
                 ("kind",))
        self.add("profiling", "Read Ctrl+F9 text. Supply length to page a stable UTF-8 snapshot and reuse its "
                 "snapshot_id and next_offset. With no slice, returns the full text.",
                 self.profiling, {"snapshot_id": integer("Profiling snapshot ID.", 1, ANY - 1),
                                  "offset": integer("UTF-8 byte offset at a character boundary.", default=0),
                                  "length": integer("Maximum UTF-8 bytes in this response.", 4, CHUNK)})
        self.add("capture", "Arm a targeted live draw or buffer capture. Poll the returned ID and read its bytes.",
                 self.arm, selector, ("kind",))
        self.add("capture_status", "Read the targeted capture state and binding metadata.",
                 lambda p, capture_id: p.request("POLL", capture_id),
                 {"capture_id": integer("Capture ID.", 1, ANY - 1)}, ("capture_id",))
        self.add("capture_cancel", "Cancel a pending selector.", lambda p, capture_id: p.request("CANCEL", capture_id),
                 {"capture_id": integer("Capture ID.", 1, ANY - 1)}, ("capture_id",))
        self.add("capture_bytes", "Read a bounded chunk of live captured bytes as base64.", self.read_bytes,
                 {"capture_id": integer("Capture ID.", 1, ANY - 1), **paging,
                  "length": integer("Bytes in one chunk.", 1, CHUNK, CHUNK)}, ("capture_id",))
        self.add("capture_save", "Save a completed capture to a new local file under the bridge output directory.",
                 self.save_capture, {"capture_id": integer("Capture ID.", 1, ANY - 1)}, ("capture_id",))
        self.add("custom_buffers", "List declared namespaced CustomResource sections.",
                 lambda p, offset=0: p.request("CUSTOMS", offset), paging)
        self.add("custom_buffer", "Read a live initialized mod buffer by its namespaced Resource name.",
                 lambda p, name, offset=0, length=0: p.request("CUSTOM", name, offset, length, timeout=30),
                 {"name": string("CustomResource section ID."), **paging, "length": selector["length"]}, ("name",))
        self.add("shaders", "List captured DXBC, including custom shaders, with their native shader hash and catalog IDs.",
                 lambda p, offset=0: p.request("SHADERS", offset), paging)
        self.add("shader", "Prepare bytecode from the shader catalog for capture_bytes or capture_save.",
                 lambda p, shader_id: p.request("SHADER", shader_id),
                 {"shader_id": integer("Shader catalog ID.", 1, ANY - 1)}, ("shader_id",))
        self.add("log_tail", "Read the tail of the main DX11 log or the current native F8 log.",
                 self.log_tail, {"source": choice("Log source.", "runtime", "f8", default="runtime"),
                                 "lines": integer("Lines to return.", 1, 300, 100)})
        self.add("frame_analysis_files", "List files from the current native F8 directory for fallback.",
                 self.analysis_files, paging)
        self.add("frame_analysis_read", "Read a chunk of a file from the current native F8 directory.",
                 self.analysis_read, {"name": string("File name from frame_analysis_files."), **paging,
                                      "length": integer("Bytes in one chunk.", 1, CHUNK, CHUNK)}, ("name",))

    def submit(self, name, arguments):
        schema, handler, pipe = self.tools[name]
        if not isinstance(arguments, dict):
            raise ValueError("Tool arguments must be an object.")
        arguments = arguments.copy()
        definitions = schema["inputSchema"]
        if set(arguments) - definitions["properties"].keys() or set(definitions["required"]) - arguments.keys():
            raise ValueError("Arguments do not match the tool schema.")
        for key, value in arguments.items():
            prop = definitions["properties"][key]
            if prop["type"] == "integer" and (type(value) is not int or not prop["minimum"] <= value <= prop["maximum"]):
                raise ValueError(f"{key} is outside its integer range.")
            if prop["type"] == "boolean" and type(value) is not bool:
                raise ValueError(f"{key} must be a boolean.")
            if prop["type"] == "string" and (not isinstance(value, str)
                                            or ("enum" in prop and value not in prop["enum"])
                                            or not prop.get("minLength", 0) <= len(value) <= prop.get("maxLength", 1024)):
                raise ValueError(f"{key} is outside its allowed string values.")
        if not pipe:
            if name == "dx11_discover":
                result = Future()
                result.set_result(handler(**arguments))
                return result
            return self.monitor_worker.submit(handler, **arguments)
        pid, target_id = self.select_target(arguments.pop("pid", None), arguments.pop("target_id", None))
        if pid not in self.connections:
            live = self.live_pids()
            self.prune_connections(live)
            if pid not in live:
                raise PipeError(f"DX11 endpoint for PID {pid} is unavailable. Rediscover running instances.",
                                "TARGET_UNAVAILABLE")
            self.connections[pid] = TargetConnection(pid, self.process_query.target)
        return self.connections[pid].submit(handler, target_id, arguments)

    def call(self, name, arguments):
        return self.submit(name, arguments).result()

    @staticmethod
    def hotkey(pipe, action, phase="tap", confirm_wipe=False):
        return pipe.request("KEY", action, phase, int(confirm_wipe), timeout=30)

    @staticmethod
    def profiling(pipe, snapshot_id=None, offset=0, length=None):
        try:
            snapshot = (pipe.request("PROFILE_BEGIN") if snapshot_id is None else
                        pipe.request("PROFILE_META", snapshot_id))
        except PipeError as error:
            if snapshot_id is None and not offset and length is None and str(error).startswith("invalid request,"):
                return pipe.request("PROFILE")  # Pre-chunking DLL.
            raise
        total = snapshot["total_bytes"]
        if offset > total:
            raise PipeError("Offset exceeds the profiling snapshot size", "PROFILE_RANGE_INVALID")
        parts = []
        next_offset = offset
        while next_offset < total:
            part = pipe.request("PROFILE_READ", snapshot["snapshot_id"], next_offset, length or SAVE_CHUNK)
            if not isinstance(part, bytes) or not part or next_offset + len(part) > total:
                raise PipeError("DX11 returned an invalid profiling chunk")
            parts.append(part)
            next_offset += len(part)
            if length is not None:
                break
        return {**snapshot, "offset": offset, "next_offset": next_offset,
                "text": b"".join(parts).decode("utf-8")}

    @staticmethod
    def arm(pipe, kind, hash=None, hash_mode="auto", stage="", slot=ANY, draw=ANY, resource_id=ANY,
            source="any", frames=30, offset=0, length=0, phase="auto", indexed_range=False):
        if hash is not None:
            normalized = hash.lower().removeprefix("0x")
            if not 1 <= len(normalized) <= 8 or any(c not in "0123456789abcdef" for c in normalized):
                raise ValueError("Expected up to 8 hexadecimal digits for the buffer hash.")
            hash = normalized
        if indexed_range and (kind != "ib" or offset or length):
            raise PipeError("Indexed range requires kind=ib without a byte slice", "INDEXED_RANGE_INVALID")
        fields = (kind, hash if hash is not None else f"{ANY:x}", stage, slot,
                  draw, frames, offset, length, hash_mode, resource_id, source, phase)
        return pipe.request("ARM", *fields, 1) if indexed_range else pipe.request("ARM", *fields)

    @staticmethod
    def read_bytes(pipe, capture_id, offset=0, length=CHUNK):
        capture = pipe.request("POLL", capture_id)
        if capture["state"] != "ready" or offset > capture["bytes"]:
            raise PipeError("Capture is not ready or offset is out of bounds.")
        chunk = pipe.request("BYTES", capture_id, offset, min(length, capture["bytes"] - offset))
        return {"capture_id": capture_id, "offset": offset, "next_offset": offset + len(chunk),
                "total_bytes": capture["bytes"], "bytes_base64": base64.b64encode(chunk).decode("ascii")}

    def save_capture(self, pipe, capture_id):
        capture = pipe.request("POLL", capture_id)
        if capture["state"] != "ready" or not capture["bytes"]:
            raise PipeError("Capture does not have a completed byte payload.")
        self.output_dir.mkdir(parents=True, exist_ok=True)
        extension = ".dxbc" if "shader_id" in capture["capture"] else ".bin"
        path = self.output_dir / f"dx11-{pipe.pid}-{capture_id}{extension}"
        with path.open("xb") as output:
            try:
                for offset in range(0, capture["bytes"], SAVE_CHUNK):
                    data = pipe.request("BYTES", capture_id, offset, min(SAVE_CHUNK, capture["bytes"] - offset))
                    output.write(data)
            except Exception:
                output.close()
                path.unlink()
                raise
        return {**capture, "path": str(path)}

    @staticmethod
    def log_tail(pipe, source="runtime", lines=100):
        state = pipe.request("STATUS")
        root = state["frame_analysis_dir"]
        if source == "f8" and not root:
            raise PipeError("No native F8 capture directory is available.")
        path = Path(root) / "log.txt" if source == "f8" else Path(state["log_path"])
        with path.open("rb") as stream:
            size = stream.seek(0, os.SEEK_END)
            stream.seek(max(0, size - 65536))
            raw = stream.read()
        return {"source": source, "path": str(path), "lines": raw.decode("utf-8", errors="replace").splitlines()[-lines:]}

    @staticmethod
    def analysis_files(pipe, offset=0):
        root = pipe.request("STATUS")["frame_analysis_dir"]
        if not root:
            raise PipeError("No native F8 capture directory is available.")
        files = sorted(path.name for path in Path(root).iterdir() if path.is_file())
        return {"directory": root, "total": len(files), "files": files[offset:offset + 128]}

    @staticmethod
    def analysis_read(pipe, name, offset=0, length=CHUNK):
        root = pipe.request("STATUS")["frame_analysis_dir"]
        if not root:
            raise PipeError("No native F8 capture directory is available.")
        directory = Path(root).resolve(strict=True)
        if Path(name).name != name or ":" in name:
            raise ValueError("Use a file name from frame_analysis_files.")
        path = (directory / name).resolve(strict=True)
        if path.parent != directory or not path.is_file():
            raise ValueError("The requested F8 file is outside the capture directory.")
        with path.open("rb") as stream:
            size = stream.seek(0, os.SEEK_END)
            if offset > size:
                raise ValueError("Offset exceeds current F8 file size.")
            stream.seek(offset)
            chunk = stream.read(length)
        return {"name": name, "offset": offset, "next_offset": offset + len(chunk), "total_bytes": size,
                "bytes_base64": base64.b64encode(chunk).decode("ascii")}


def serve(bridge):
    output_lock = Lock()

    def emit(reply):
        with output_lock:
            print(json.dumps(reply, ensure_ascii=True), flush=True)

    def tool_error(error):
        result = {"isError": True, "content": [{"type": "text", "text": str(error)}]}
        if isinstance(error, PipeError) and error.code:
            result["structuredContent"] = {"code": error.code, "message": str(error)}
        return result

    def complete(future, request_id):
        try:
            result = {"content": [{"type": "text", "text": json.dumps(future.result(), ensure_ascii=False)}]}
        except (OSError, ValueError, PipeError, KeyError) as error:
            result = tool_error(error)
        except Exception as error:
            emit({"jsonrpc": "2.0", "id": request_id, "error": {"code": -32603, "message": str(error)}})
            return
        emit({"jsonrpc": "2.0", "id": request_id, "result": result})

    for line in sys.stdin:
        request = None
        try:
            request = json.loads(line)
            method = request["method"]
            params = request.get("params", {})
            if "id" not in request:
                continue
            if method == "initialize":
                supported = {"2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25"}
                version = params.get("protocolVersion")
                result = {"protocolVersion": version if version in supported else "2025-11-25",
                          "capabilities": {"tools": {}}, "serverInfo": {"name": "projectbunny-dx11", "version": "1.0"}}
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = {"tools": [tool[0] for tool in bridge.tools.values()]}
            elif method == "tools/call":
                try:
                    future = bridge.submit(params["name"], params.get("arguments", {}))
                except (OSError, ValueError, PipeError, KeyError) as error:
                    result = tool_error(error)
                else:
                    future.add_done_callback(lambda done, request_id=request["id"]: complete(done, request_id))
                    continue
            else:
                reply = {"jsonrpc": "2.0", "id": request["id"], "error": {"code": -32601, "message": "Method not found"}}
                emit(reply)
                continue
            reply = {"jsonrpc": "2.0", "id": request["id"], "result": result}
        except (ValueError, KeyError, TypeError, AttributeError):
            reply = {"jsonrpc": "2.0", "id": request.get("id") if isinstance(request, dict) else None,
                     "error": {"code": -32600, "message": "Invalid JSON-RPC request"}}
        emit(reply)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, help="Default game process ID")
    parser.add_argument("--monitor", action="store_true", help="Arm the startup log and process monitor immediately")
    parser.add_argument("--monitor-exe", help="Restrict monitoring to this executable file name")
    parser.add_argument("--monitor-level", choices=LEVELS, default="warning")
    parser.add_argument("--output-dir", type=Path,
                        default=Path(__file__).resolve().parents[1] / "build" / "mcp-captures")
    args = parser.parse_args()
    sys.stdin.reconfigure(encoding="utf-8")
    bridge = Bridge(args.pid, args.output_dir.resolve())
    try:
        if args.monitor or args.monitor_exe:
            bridge.monitor.start(args.monitor_exe, args.monitor_level)
        serve(bridge)
    finally:
        bridge.close()
