"""Exercise the version test's real reader with bounded native sharing faults."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
from threading import Event
from unittest.mock import patch
import os


def check_reader(work):
    spec = importlib.util.spec_from_file_location("version_test", Path(__file__).with_name("test-dx11-version.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    output = work / "sharing.h"
    output.write_text("complete header\n", encoding="utf-8")
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        native = ctypes.WinDLL("kernel32", use_last_error=True)
        native.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
            wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        native.CreateFileW.restype = wintypes.HANDLE
        native.CloseHandle.argtypes = [wintypes.HANDLE]
        native.CloseHandle.restype = wintypes.BOOL

        def hold():
            handle = native.CreateFileW(str(output), 0x80000000, 0, None, 3, 0, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            return handle

        def close(handle):
            if not native.CloseHandle(handle):
                raise ctypes.WinError(ctypes.get_last_error())

        denied = Event()
        failures = []
        actual_read = Path.read_text

        def observe(path, *args, **kwargs):
            assert path == output, "retry must use the same generated path"
            try:
                return actual_read(path, *args, **kwargs)
            except PermissionError as error:
                failures.append(error)
                denied.set()
                raise

        handle = hold()
        with ThreadPoolExecutor(max_workers=1) as workers:
            try:
                with patch.object(Path, "read_text", observe):
                    future = workers.submit(module.read_generated_text, output)
                    assert denied.wait(2), "real native sharing denial was not reached"
                    close(handle)
                    handle = None
                    assert future.result(timeout=3) == "complete header\n"
            finally:
                if handle is not None:
                    close(handle)
        assert failures, "transient fault must reach the reader"
        handle = hold()
        try:
            try:
                module.read_generated_text(output, timeout=0.05)
            except PermissionError:
                pass
            else:
                raise AssertionError("permanent sharing denial was swallowed")
        finally:
            close(handle)
        print("PASS: native transient sharing denial recovered; permanent denial raised")
    else:
        print("SKIP: native Windows sharing cases")
    invalid = work / "invalid.h"
    invalid.write_bytes(b"\xff")
    for path, expected in ((work / "missing.h", FileNotFoundError), (invalid, UnicodeDecodeError)):
        with patch.object(module.time, "sleep", side_effect=AssertionError("non-permission errors must not retry")):
            try:
                module.read_generated_text(path)
            except expected:
                pass
            else:
                raise AssertionError(f"{expected.__name__} was swallowed")
    print("PASS: missing-file and decoding errors propagate without retry")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--child", action="store_true")
    args = parser.parse_args()
    work = args.work_dir.resolve()
    source_root = args.source_root.resolve()
    work.relative_to(source_root / "build")
    if args.child:
        check_reader(work)
        return
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="reader-sharing-", dir=work) as temporary:
        result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--child", "--source-root", str(source_root), "--work-dir", temporary],
            capture_output=True, text=True, encoding="utf-8", timeout=10,
            env={**os.environ, "PYTHONIOENCODING": "utf-8"})
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    main()
