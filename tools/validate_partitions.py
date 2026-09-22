#!/usr/bin/env python3
"""Validate the ESP32-C3 partition table against the configured flash size.

Checks performed:
  * every partition lies within the configured flash size
  * partitions do not overlap (auto-placed offsets are resolved)
  * the app image (firmware.bin) fits inside an OTA app slot, when provided

Run from the repository root:

    python tools/validate_partitions.py [--firmware path/to/firmware.bin]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PARTITIONS_CSV = REPO_ROOT / "partitions.csv"
PLATFORMIO_INI = REPO_ROOT / "platformio.ini"

# The bootloader and partition table live below the first partition.
FIRST_PARTITION_OFFSET = 0x9000

_SIZE_UNITS = {"K": 1024, "M": 1024 * 1024}


def parse_size(value: str) -> int:
    value = value.strip().upper()
    if value.endswith("B"):
        value = value[:-1]
    if value and value[-1] in _SIZE_UNITS:
        return int(value[:-1]) * _SIZE_UNITS[value[-1]]
    return int(value, 0)


def parse_flash_size(ini_path: Path) -> int:
    for line in ini_path.read_text().splitlines():
        match = re.match(r"\s*board_upload\.flash_size\s*=\s*(\S+)", line)
        if match:
            return parse_size(match.group(1))
    raise SystemExit("Could not determine flash size from platformio.ini")


def parse_partitions(csv_path: Path) -> list[dict]:
    partitions = []
    for lineno, raw in enumerate(csv_path.read_text().splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            raise SystemExit(f"partitions.csv:{lineno}: expected at least 5 fields")
        name, ptype, subtype, offset, size = fields[:5]
        partitions.append(
            {
                "name": name,
                "type": ptype,
                "subtype": subtype,
                "offset": parse_size(offset) if offset else None,
                "size": parse_size(size),
            }
        )
    if not partitions:
        raise SystemExit("partitions.csv contains no partitions")
    return partitions


def validate(firmware: Path | None) -> None:
    flash_size = parse_flash_size(PLATFORMIO_INI)
    partitions = parse_partitions(PARTITIONS_CSV)

    errors: list[str] = []
    cursor = FIRST_PARTITION_OFFSET
    app_slots: list[dict] = []

    for part in partitions:
        if part["offset"] is None:
            part["offset"] = cursor
        if part["offset"] < cursor:
            errors.append(
                f"partition '{part['name']}' at 0x{part['offset']:x} overlaps the "
                f"previous partition (ends at 0x{cursor:x})"
            )
        end = part["offset"] + part["size"]
        if end > flash_size:
            errors.append(
                f"partition '{part['name']}' ends at 0x{end:x}, beyond the "
                f"{flash_size // 1024 // 1024} MB flash (0x{flash_size:x})"
            )
        if part["type"] == "app":
            app_slots.append(part)
        cursor = max(cursor, end)

    if firmware is not None:
        image_size = firmware.stat().st_size
        slot = next((p for p in app_slots if image_size <= p["size"]), None)
        if slot is None:
            errors.append(
                f"firmware image ({image_size} bytes) does not fit in any app "
                f"partition (largest is {max(p['size'] for p in app_slots)} bytes)"
            )
        else:
            print(
                f"firmware image {image_size} bytes fits in '{slot['name']}' "
                f"({slot['size']} bytes)"
            )

    print(f"flash size: {flash_size} bytes ({flash_size // 1024 // 1024} MB)")
    for part in partitions:
        end = part["offset"] + part["size"]
        print(
            f"  {part['name']:<10} {part['type']:<5} {part['subtype']:<8} "
            f"0x{part['offset']:06x} .. 0x{end:06x} ({part['size'] // 1024} KiB)"
        )

    if errors:
        print("\nPartition validation FAILED:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        raise SystemExit(1)

    print("\nPartition validation passed.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--firmware",
        type=Path,
        default=None,
        help="optional path to firmware.bin to size-check against app slots",
    )
    args = parser.parse_args()
    validate(args.firmware)


if __name__ == "__main__":
    main()
