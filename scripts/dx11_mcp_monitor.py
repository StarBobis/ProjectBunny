"""Receive DX11 diagnostics before the render control pipe exists."""

from collections import OrderedDict, deque
import ctypes as C
from ctypes import wintypes as W
from pathlib import PureWindowsPath
import struct
import threading
import time


MAPPING = r"Local\ProjectBunny-DX11-Monitor-v1"
MAGIC, VERSION, CAPACITY, RECORD_SIZE = 0x4D444250, 1, 4096, 560
HEADER = struct.Struct("<IIIIqiiq")
RECORD = struct.Struct("<IIqIIQQII512s")
SIZE = HEADER.size + CAPACITY * RECORD_SIZE
LEVELS = {"warning": 1, "info": 2, "debug": 3}
# The native DX11 producer emits a Present event at most once per second.
PRESENT_HEARTBEAT_MS = 1000
MIN_PRESENT_STALL_MS = 2 * PRESENT_HEARTBEAT_MS
KINDS = {1: "warning", 2: "info", 3: "debug", 10: "phase", 11: "present",
         12: "attach", 13: "detach", 14: "failure", 15: "identity"}


class Transport:
    def __init__(self):
        self.api = C.WinDLL("kernel32", use_last_error=True)
        pointer = C.c_void_p
        filetime = C.POINTER(W.FILETIME)
        self.bind("CreateMutexW", W.HANDLE, pointer, W.BOOL, W.LPCWSTR)
        self.bind("CreateFileMappingW", W.HANDLE, W.HANDLE, pointer, W.DWORD, W.DWORD, W.DWORD, W.LPCWSTR)
        self.bind("MapViewOfFile", pointer, W.HANDLE, W.DWORD, W.DWORD, W.DWORD, C.c_size_t)
        self.bind("UnmapViewOfFile", W.BOOL, pointer)
        self.bind("CloseHandle", W.BOOL, W.HANDLE)
        self.bind("GetTickCount64", C.c_uint64)
        self.bind("OpenProcess", W.HANDLE, W.DWORD, W.BOOL, W.DWORD)
        self.bind("QueryFullProcessImageNameW", W.BOOL, W.HANDLE, W.DWORD,
                  W.LPWSTR, C.POINTER(W.DWORD))
        self.bind("GetProcessTimes", W.BOOL, W.HANDLE, filetime, filetime, filetime, filetime)
        self.bind("WaitForSingleObject", W.DWORD, W.HANDLE, W.DWORD)
        self.bind("GetExitCodeProcess", W.BOOL, W.HANDLE, C.POINTER(W.DWORD))
        self.owner = self.mapping = self.view = None

    def bind(self, name, output, *inputs):
        fn = getattr(self.api, name)
        fn.argtypes = inputs
        fn.restype = output

    def open(self):
        if C.sizeof(C.c_void_p) != 8:
            raise OSError("DX11 monitoring requires 64-bit Python.")
        self.owner = self.api.CreateMutexW(None, False, MAPPING + "-owner")
        error = C.get_last_error()
        if not self.owner:
            raise C.WinError(error)
        try:
            if error == 183:
                raise OSError("Another bridge already owns the DX11 monitor in this Windows session.")
            self.mapping = self.api.CreateFileMappingW(C.c_void_p(-1), None, 4, 0, SIZE, MAPPING)
            existing = C.get_last_error() == 183
            if not self.mapping:
                raise C.WinError(C.get_last_error())
            self.view = self.api.MapViewOfFile(self.mapping, 2, 0, 0, SIZE)  # FILE_MAP_WRITE also permits reads.
            if not self.view:
                raise C.WinError(C.get_last_error())
            if existing:
                if HEADER.unpack(C.string_at(self.view, HEADER.size))[:4] != (
                        MAGIC, VERSION, CAPACITY, RECORD_SIZE):
                    raise OSError("DX11 monitor shared memory has an incompatible layout.")
            else:
                header = HEADER.pack(0, VERSION, CAPACITY, RECORD_SIZE, 0, 0, 0, 0)
                C.memmove(self.view, header, len(header))
                C.c_uint32.from_address(self.view).value = MAGIC
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.view:
            self.api.UnmapViewOfFile(self.view)
        if self.mapping:
            self.api.CloseHandle(self.mapping)
        if self.owner:
            self.api.CloseHandle(self.owner)
        self.owner = self.mapping = self.view = None

    def enable(self, level):
        if level:
            self.renew()
        C.c_int32.from_address(self.view + 28).value = level
        C.c_int32.from_address(self.view + 24).value = int(level != 0)

    def renew(self):
        C.c_int64.from_address(self.view + 32).value = self.api.GetTickCount64()

    def head(self):
        return C.c_int64.from_address(self.view + 16).value

    def record(self, sequence):
        address = self.view + HEADER.size + ((sequence - 1) % CAPACITY) * RECORD_SIZE
        busy = C.c_int32.from_address(address)
        committed = C.c_int64.from_address(address + 8)
        if busy.value or committed.value != sequence:
            return None
        raw = C.string_at(address, RECORD_SIZE)
        if busy.value or committed.value != sequence:
            return None
        locked, kind, number, pid, tid, tick, started, detail, size, payload = RECORD.unpack(raw)
        if locked or number != sequence or kind not in KINDS or size > len(payload):
            return None
        text = payload[:size]
        try:
            message = text.decode("utf-8")
        except UnicodeDecodeError:
            message = text.decode("mbcs", errors="replace")
        return {"source_seq": number, "pid": pid, "tid": tid, "tick_ms": tick, "process_start": started,
                "kind": KINDS[kind], "detail": detail, "message": message.rstrip("\r\n")}

    def process(self, pid, started):
        handle = self.api.OpenProcess(0x101000, False, pid)  # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
        if not handle and C.get_last_error() == 5:
            handle = self.api.OpenProcess(0x100000, False, pid)
        if not handle:
            return None, C.get_last_error() == 87
        actual = self.creation_time(handle) if started else 0
        if actual and actual != started:
            self.api.CloseHandle(handle)
            return None, True  # PID was reused before the reader could open it.
        return handle, False

    def creation_time(self, handle):
        times = [W.FILETIME() for _ in range(4)]
        if self.api.GetProcessTimes(handle, *(C.byref(item) for item in times)):
            return (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        return 0

    def exit_code(self, handle):
        if self.api.WaitForSingleObject(handle, 0) != 0:
            return False, None
        code = W.DWORD()
        return True, code.value if self.api.GetExitCodeProcess(handle, C.byref(code)) else None

    def executable(self, handle, started):
        if not handle or not started:
            return None
        if self.creation_time(handle) != started:
            return None
        path = C.create_unicode_buffer(32768)
        size = W.DWORD(len(path))
        if not self.api.QueryFullProcessImageNameW(handle, 0, path, C.byref(size)):
            return None
        return PureWindowsPath(path.value).name

    def target(self, pid):
        """Describe a pipe owner without waiting for its rendering thread."""
        handle, _ = self.process(pid, 0)
        started, executable = 0, None
        if handle:
            try:
                started = self.creation_time(handle)
                if started:
                    executable = self.executable(handle, started)
            finally:
                self.api.CloseHandle(handle)
        return {"pid": pid, "executable": executable,
                "target_id": f"{pid}:{started:016x}" if started else None}


class Monitor:
    def __init__(self):
        self.transport = None
        self.events = deque(maxlen=8192)
        self.sessions = OrderedDict()
        self.identity_retry = OrderedDict()
        self.condition = threading.Condition()
        self.kick = threading.Event()
        self.closed = threading.Event()
        self.thread = None
        self.active = False
        self.target_exe = None
        self.stall_ms = 5000
        self.seq = self.source_seq = self.dropped = 0
        self.missing_since = None
        self.error = None

    def start(self, target_exe=None, level="warning", stall_ms=5000):
        if target_exe and PureWindowsPath(target_exe).name != target_exe:
            raise ValueError("target_exe must be an executable name without a directory.")
        with self.condition:
            if self.error:
                raise OSError(self.error)
            if not self.transport:
                transport = Transport()
                transport.open()
                self.transport = transport
                self.source_seq = max(0, transport.head() - CAPACITY)
                self.thread = threading.Thread(target=self.run, name="dx11-monitor", daemon=True)
                self.thread.start()
            target = target_exe.casefold() if target_exe else None
            if target != self.target_exe:
                self.identity_retry.clear()
            self.target_exe = target
            self.stall_ms = stall_ms
            self.active = True
            self.transport.enable(LEVELS[level])
            self.kick.set()
            return {"state": "monitoring", "target_exe": target_exe, "level": level, "stall_ms": stall_ms,
                    "present_stall_ms": max(stall_ms, MIN_PRESENT_STALL_MS),
                    "next_seq": self.seq, "sessions": self.statuses()}

    def append(self, event):
        self.seq += 1
        event["seq"] = self.seq
        self.events.append(event)

    def mark_exit(self, session, code):
        session["exited"] = True
        session["exit_code"] = code
        if session["handle"]:
            self.transport.api.CloseHandle(session["handle"])
            session["handle"] = None
        self.append({"pid": session["pid"], "process_start": session["process_start"], "kind": "process_exit",
                     "tick_ms": self.transport.api.GetTickCount64(), "exit_code": code,
                     "exit_code_hex": None if code is None else f"0x{code:08x}",
                     "message": "Process exited" if code is not None else "Process ended before its exit code was available"})

    def identify(self, key, event):
        now = time.monotonic()
        identity_event = event["kind"] in ("attach", "identity")
        if not identity_event and now < self.identity_retry.get(key, 0):
            return None
        handle, gone = None, False
        if identity_event and self.target_exe and event["message"].casefold() != self.target_exe:
            executable = event["message"]
        else:
            handle, gone = self.transport.process(*key)
            executable = event["message"] if identity_event else self.transport.executable(handle, key[1])
        if not executable or (self.target_exe and executable.casefold() != self.target_exe):
            if handle:
                self.transport.api.CloseHandle(handle)
            self.identity_retry[key] = now + 1
            self.identity_retry.move_to_end(key)
            if len(self.identity_retry) > 256:
                self.identity_retry.popitem(last=False)
            return None
        self.identity_retry.pop(key, None)
        if len(self.sessions) >= 64:
            oldest = next((k for k, v in self.sessions.items() if v["exited"]), next(iter(self.sessions)))
            retired = self.sessions.pop(oldest)
            if retired["handle"]:
                self.transport.api.CloseHandle(retired["handle"])
        session = dict(pid=key[0], process_start=key[1], executable=executable, handle=handle,
                       gone=gone, exited=False, exit_code=None, phase="dll_attaching" if event["kind"] == "attach" else "observed_running",
                       identity_source=event["kind"] if identity_event else "process_image",
                       progress=event["tick_ms"], frame=None, detached=False,
                       process_retry=now + 1)
        self.sessions[key] = session
        return session

    def accept(self, event):
        key = (event["pid"], event["process_start"])
        session = self.sessions.get(key) or self.identify(key, event)
        if session is None or (self.target_exe and session["executable"].casefold() != self.target_exe):
            return
        if event["kind"] in ("phase", "present", "attach", "detach", "failure"):
            if event["kind"] == "present":
                session["frame"] = event["detail"]
                session["phase"] = "present"
            elif event["kind"] != "attach":
                session["phase"] = event["message"]
                session["detached"] = event["kind"] == "detach" and event["message"] == "dll_unloaded"
            session["progress"] = max(session["progress"], event["tick_ms"])
        self.append(event)

    def run(self):
        try:
            while not self.closed.is_set():
                self.kick.wait(0.025)
                self.kick.clear()
                with self.condition:
                    if not self.active:
                        continue
                    self.transport.renew()
                    now = time.monotonic()
                    head = self.transport.head()
                    overflow = max(0, head - CAPACITY - self.source_seq)
                    if overflow:
                        self.dropped += overflow
                        self.source_seq += overflow
                        self.missing_since = None
                    last = self.seq
                    while self.source_seq < head:
                        record = self.transport.record(self.source_seq + 1)
                        if record:
                            self.missing_since = None
                            self.accept(record)
                        elif self.missing_since is None:
                            self.missing_since = now
                            break
                        elif now - self.missing_since < 0.1:
                            break
                        else:
                            self.dropped += 1
                        self.source_seq += 1
                    for session in self.sessions.values():
                        if session["exited"]:
                            continue
                        if not session["handle"] and not session["gone"] and now >= session["process_retry"]:
                            handle, gone = self.transport.process(session["pid"], session["process_start"])
                            session.update(handle=handle, gone=gone, process_retry=now + 1)
                        if session["gone"]:
                            if self.source_seq >= head:
                                self.mark_exit(session, None)
                        elif session["handle"]:
                            exited, code = self.transport.exit_code(session["handle"])
                            if exited and self.source_seq >= head:
                                self.mark_exit(session, code)
                    if last != self.seq:
                        self.condition.notify_all()
        except Exception as error:
            with self.condition:
                self.error = f"DX11 monitoring stopped: {error}"
                self.active = False
                self.transport.enable(0)
                self.condition.notify_all()

    def statuses(self, pid=None):
        if not self.transport:
            return []
        now = self.transport.api.GetTickCount64()
        results = []
        for session in self.sessions.values():
            if pid is not None and session["pid"] != pid:
                continue
            age = max(0, now - session["progress"])
            threshold = self.stall_ms if session["frame"] is None else max(self.stall_ms, MIN_PRESENT_STALL_MS)
            if session["exited"]:
                state = "exited"
            elif session["detached"]:
                state = "dll_unloaded"
            elif age >= threshold:
                state = "no_progress"
            else:
                state = "presenting" if session["frame"] is not None else "loading"
            results.append({"pid": session["pid"], "process_start": session["process_start"],
                            "executable": session["executable"], "state": state, "phase": session["phase"],
                            "identity_source": session["identity_source"],
                            "stall_threshold_ms": threshold,
                            "progress_age_ms": age, "last_frame": session["frame"], "exit_code": session["exit_code"]})
        return results

    def poll(self, after_seq=0, limit=128, wait_ms=0, pid=None):
        with self.condition:
            if not self.transport:
                raise ValueError("Start dx11_monitor_start before launching the game.")
            if after_seq > self.seq:
                raise ValueError("after_seq is ahead of this monitor. Start reading again at 0.")
            if wait_ms and self.active:
                self.condition.wait_for(
                    lambda: not self.active or any(item["seq"] > after_seq and (pid is None or item["pid"] == pid)
                                                   for item in reversed(self.events)), wait_ms / 1000)
            oldest = self.events[0]["seq"] if self.events else self.seq + 1
            events = [item for item in self.events if item["seq"] > after_seq and
                      (pid is None or item["pid"] == pid)][:limit]
            states = self.statuses(pid)
            running = any(state["state"] not in ("exited", "dll_unloaded") for state in states)
            return {"state": "monitoring" if self.active else "stopped",
                    "waiting_for_dll": self.active and not running, "sessions": states, "events": events,
                    "next_seq": events[-1]["seq"] if len(events) == limit else self.seq,
                    "transport_dropped": self.dropped, "history_lost": max(0, oldest - 1 - after_seq),
                    "monitor_error": self.error}

    def stop(self):
        with self.condition:
            self.active = False
            if self.transport:
                self.transport.enable(0)
            self.condition.notify_all()
            return {"state": "stopped", "last_seq": self.seq}

    def close(self):
        self.stop()
        self.closed.set()
        self.kick.set()
        if self.thread:
            self.thread.join()
        if self.transport:
            for session in self.sessions.values():
                if session["handle"]:
                    self.transport.api.CloseHandle(session["handle"])
            self.transport.close()
