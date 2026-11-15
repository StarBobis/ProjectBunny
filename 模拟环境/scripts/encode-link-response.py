"""Normalize one generated Ninja/MSVC link response, never production inputs."""
from pathlib import Path
import sys
import subprocess

build_root = Path(sys.argv[1]).resolve(strict=True)
response = Path(sys.argv[2]).resolve(strict=True)
expected = build_root / "CMakeFiles" / "dx11_upstream_regression.rsp"
if response != expected or not any(part.lower() == "build" for part in build_root.parts):
    raise RuntimeError("Refusing to write outside the exact generated build response")
raw = response.read_bytes()
if not raw.startswith((b"\xff\xfe", b"\xfe\xff")):
    text = raw.decode("utf-8-sig", errors="strict")
    response.write_bytes(text.encode("utf-16"))
print("Prepared UTF-16 MSVC response for the business regression linker")
if len(sys.argv) < 4:
    raise RuntimeError("Missing original linker command")
sys.exit(subprocess.run(sys.argv[3:], check=False).returncode)
