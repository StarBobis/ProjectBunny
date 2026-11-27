"""Verify the APMI UnityPlayer palette queue path without opening the game process."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import struct


UNITY_SHA256 = "ce1544f853cf32ba91c705cdc4f6c168a8a4147e05bb62b938a55a1393a4a70e"
OPCODE = 0x274B
JUMP_TABLE = 0x111F47C
OPCODE_BASE = 0x2711
CONSUMER_CASE = 0x111E08F
PRODUCER_ENTRY = 0xA129F0
BACKEND_UPLOAD = 0x93DB50
GRAPHICS_SLOT = 0x3A8

WITNESSES = {
    0x54D067: "48c1e306",          # Convert the pose count to bytes.
    0x54D12A: "488d05afc14e01",    # Address the compute skin pose buffer error string.
    0x54D208: "ff90a8030000",      # Submit using the graphics-device virtual slot.
    0xA12A6A: "c704014b270000",    # Write the palette command tag.
    0xA12AC7: "48893401",          # Write the first payload reference.
    0xA12B26: "48892c01",          # Write the second payload reference.
    0xA12B40: "448981c0000000",    # Publish the advanced write position.
    0x1112744: "448b140a",         # Read the next tag on the worker.
    0x111274F: "418d8aefd8ffff",   # Subtract the first opcode.
    0x1112756: "81f9f2000000",     # Check the dispatch index.
    0x1112773: "418b8c907cf41101", # Fetch the case RVA.
    0x111E0EC: "488b1c01",         # Load the first aligned payload reference.
    0x111E158: "4f8b0408",         # Load the second aligned payload reference.
    0x111E163: "41ffd2",           # Call the backend graphics-device slot.
    0x740ADA: "49c1e106",          # Convert the backend pose count to bytes.
    0x740AFA: "ff9080080000",      # Call the backend buffer writer.
    0x93DD24: "ff9080010000",      # ID3D11DeviceContext::UpdateSubresource.
}

LIVE_STACK = (
    ("unityplayer.dll", 0x93DD2A),
    ("unityplayer.dll", 0x740B00),
    ("unityplayer.dll", 0x111E166),
    ("unityplayer.dll", 0x11125AF),
    ("unityplayer.dll", 0x11124F1),
    ("unityplayer.dll", 0x6E797E),
    ("kernel32.dll", 0x17124),
    ("ntdll.dll", 0x4CC91),
)


def hex_rva(value):
    return f"0x{value:x}"


class Image:
    def __init__(self, path):
        self.data = path.read_bytes()
        self.sha256 = sha256(self.data).hexdigest()
        if self.sha256 != UNITY_SHA256:
            raise ValueError(f"Unsupported UnityPlayer SHA256 {self.sha256}")
        pe_offset, = struct.unpack_from("<I", self.data, 0x3C)
        magic, machine, count, _, _, _, optional_size, _ = struct.unpack_from("<4sHHIIIHH", self.data, pe_offset)
        optional = pe_offset + 24
        if magic != b"PE\0\0" or machine != 0x8664 or self.data[optional:optional + 2] != b"\x0b\x02":
            raise ValueError("Expected an x64 PE image")
        self.base, = struct.unpack_from("<Q", self.data, optional + 24)
        table = optional + optional_size
        self.sections = [
            struct.unpack_from("<IIII", self.data, table + i * 40 + 8)
            for i in range(count)
        ]

    def at(self, rva, size):
        for _, address, raw_size, offset in self.sections:
            relative = rva - address
            if 0 <= relative <= raw_size - size:
                return self.data[offset + relative:offset + relative + size]
        raise ValueError(f"Unmapped RVA {hex_rva(rva)}")

    def u32(self, rva):
        return struct.unpack("<I", self.at(rva, 4))[0]

    def pointer_rva(self, rva):
        return struct.unpack("<Q", self.at(rva, 8))[0] - self.base


def verify_image(path):
    image = Image(path)
    for address, encoded in WITNESSES.items():
        expected = bytes.fromhex(encoded)
        if image.at(address, len(expected)) != expected:
            raise ValueError(f"Code witness mismatch at {hex_rva(address)}")

    index = OPCODE - OPCODE_BASE
    if image.u32(JUMP_TABLE + index * 4) != CONSUMER_CASE:
        raise ValueError("The dispatch entry does not select the observed consumer case")
    for table in (0x1A7BE98, 0x1A7CB08):
        if image.pointer_rva(table + GRAPHICS_SLOT) != 0x740A80 or image.pointer_rva(table + 0x880) != BACKEND_UPLOAD:
            raise ValueError(f"Unexpected backend virtual slots in table {hex_rva(table)}")
    if image.pointer_rva(0x1ABC038 + GRAPHICS_SLOT) != PRODUCER_ENTRY:
        raise ValueError("The producer virtual slot no longer points to the command writer")

    text = b"Failed to create compute skin pose buffer!\0"
    if image.at(0x1A392E0, len(text)) != text:
        raise ValueError("Missing compute skin pose buffer error string")
    direct_calls = {0x54E66D: 0x54D230, 0x54F047: 0x54D230, 0x54D2C0: 0x54D040}
    for site, target in direct_calls.items():
        tag, displacement = struct.unpack("<Bi", image.at(site, 5))
        if tag != 0xE8 or site + 5 + displacement != target:
            raise ValueError(f"Unexpected upstream call at {hex_rva(site)}")
    return {
        "unity_player_sha256": image.sha256,
        "opcode": hex_rva(OPCODE),
        "producer_entry_rva": hex_rva(PRODUCER_ENTRY),
        "opcode_write_rva": "0xa12a6a",
        "queue_publish_rva": "0xa12b40",
        "producer_call_rva": "0x54d208",
        "upstream_direct_calls": {hex_rva(site): hex_rva(target) for site, target in direct_calls.items()},
        "consumer_case_rva": hex_rva(CONSUMER_CASE),
        "consumer_call_rva": "0x111e163",
        "dx11_upload_call_rva": "0x93dd24",
        "annotation": "Verified binary path. Producer execution and queue-instance identity are separate live observations.",
    }


def verify_evidence(path):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    uploads = []
    threads = set()
    for pair in data["comparisons"]:
        for scene in ("world", "team"):
            upload = pair[scene + "_evidence"]["upload"]
            frames = tuple((f["module"].lower(), int(f["rva"], 16)) for f in upload["caller_frames"])
            unwind = upload["caller_unwind"]
            if (frames != LIVE_STACK or unwind["native_frames"] != 12 or unwind["unresolved_frames"] or
                    unwind["native_limit_reached"] or unwind["frame_limit_reached"]):
                raise ValueError(f"Unexpected consumer stack for {scene} IB {pair[scene + '_ib']}")
            length = upload["cpu_source_bytes"]
            if (upload["api"] != "UpdateSubresource" or upload["cpu_source_state"] != "hashed" or
                    upload["destination_range"] != {"offset": 0, "bytes": length} or not length or length % 64):
                raise ValueError(f"Unexpected CPU upload range for {scene} IB {pair[scene + '_ib']}")
            threads.update((upload["record_thread"], upload["submit_thread"]))
            uploads.append({"scene": scene, "ib_hash": pair[scene + "_ib"], "resource_id": upload["resource_id"],
                            "bytes": length, "pose_slots": length // 64})
    if len(uploads) != 12 or len(threads) != 1 or data["thread_name"] != "UnityGfxDeviceWorker":
        raise ValueError("Expected twelve uploads recorded and submitted by one Unity graphics worker")
    return {"target_id": data["target"], "thread_name": data["thread_name"], "thread_id": threads.pop(),
            "uploads": uploads}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("unity_player", type=Path)
    parser.add_argument("--consumer-evidence", type=Path, help="APMI world/team caller stack comparison JSON")
    args = parser.parse_args()
    try:
        report = verify_image(args.unity_player)
        if args.consumer_evidence:
            report["consumer_evidence"] = verify_evidence(args.consumer_evidence)
    except (OSError, KeyError, TypeError, ValueError, struct.error) as error:
        parser.exit(2, f"{parser.prog}: {error}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
