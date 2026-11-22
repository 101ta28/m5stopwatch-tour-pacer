#!/usr/bin/env python3
"""Check the actual flash image against the sketch's partition table.

M5Stack Arduino 3.3.7 uploads OTA data at 0xe000 and the app at 0x10000,
even when a custom CSV relocates those partitions. Catch that before upload.
"""
import csv
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "arduino"
FIRMWARE = ROOT / "build" / "firmware"
STEM = "StopWatchTourGuide.ino"


def require(condition, message):
    if not condition:
        raise SystemExit(f"Firmware check failed: {message}")


def main():
    expected = {}
    source = ROOT / "StopWatchTourGuide" / "partitions.csv"
    for row in csv.reader(source.read_text(encoding="utf-8").splitlines()):
        if not row or row[0].lstrip().startswith("#"):
            continue
        name, kind, subtype, offset, size, *_ = (cell.strip() for cell in row)
        expected[name] = (int(offset, 0), int(size, 0))
    raw_table = (FIRMWARE / f"{STEM}.partitions.bin").read_bytes()
    actual = {}
    for position in range(0, len(raw_table), 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack_from(
            "<HBBII16sI", raw_table, position
        )
        if magic != 0x50AA:
            break
        name = label.split(b"\0", 1)[0].decode("ascii")
        require(name not in actual, f"duplicate partition {name}")
        actual[name] = (offset, size)
        require(offset % 4096 == 0 and size % 4096 == 0, f"unaligned {name}")
        if kind == 0:
            require(offset % 65536 == 0, f"unaligned application {name}")
        if name == "nvs":
            require((kind, subtype) == (1, 2), "nvs has the wrong partition type")
    require(actual == expected, "generated partition table differs from source CSV")
    require(actual["app0"][0] == 0x10000, "app0 differs from Arduino's upload address")
    require(actual["otadata"][0] == 0xE000, "OTA data differs from Arduino's upload address")
    require(actual["nvs"][1] >= 2 * 24576 + 4096 + 8192, "NVS cannot hold both records and course settings")
    previous_end = 0x9000  # End of the partition-table sector.
    for name, (offset, size) in sorted(actual.items(), key=lambda item: item[1][0]):
        require(offset >= previous_end, f"overlapping partition {name}")
        previous_end = offset + size
    require(previous_end == 16 * 1024 * 1024, "partition layout must end at 16 MiB")

    image = (FIRMWARE / f"{STEM}.merged.bin").read_bytes()
    require(len(image) == 16 * 1024 * 1024, "merged image must be 16 MiB")
    files = [
        (0, FIRMWARE / f"{STEM}.bootloader.bin"),
        (0x8000, FIRMWARE / f"{STEM}.partitions.bin"),
        (actual["app0"][0], FIRMWARE / f"{STEM}.bin"),
    ]
    options = json.loads((BUILD / "build.options.json").read_bytes())
    platform = Path(options["hardwareFolders"].split(",")[0])
    files.append((actual["otadata"][0], platform / "tools" / "partitions" / "boot_app0.bin"))
    for offset, path in files:
        data = path.read_bytes()
        require(image[offset:offset + len(data)] == data, f"wrong placement of {path.name}")
    require((FIRMWARE / f"{STEM}.bin").stat().st_size <= actual["app0"][1], "app exceeds its partition")
    offset, size = actual["nvs"]
    require(all(byte == 0xFF for byte in image[offset:offset + size]), "initial NVS image is not blank")
    print("Firmware check passed: app/OTA upload offsets, partition table, merged image, 84 KiB NVS")


if __name__ == "__main__":
    main()
