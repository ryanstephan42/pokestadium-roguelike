"""Compare canonical N64 RDRAM snapshots and label differences with map symbols."""

import argparse
import json
import struct
from pathlib import Path

from .frontend import Symbols


def compare(before, after, limit):
    before, after = Path(before), Path(after)
    left, right = before.read_bytes(), after.read_bytes()
    if len(left) != len(right):
        raise ValueError(f"Snapshot sizes differ: {len(left)} != {len(right)}")
    metadata_path = after.with_suffix(after.suffix + ".json")
    if not metadata_path.exists():
        metadata_path = before.with_suffix(before.suffix + ".json")
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else {}
    symbols = Symbols(metadata["map"]) if metadata.get("map") else None
    overlays = metadata.get("overlays", [])
    if not overlays:
        previous_metadata = before.with_suffix(before.suffix + ".json")
        if previous_metadata.exists():
            overlays = json.loads(previous_metadata.read_text()).get("overlays", [])
    runs = []
    start = previous = None
    for address, (old, new) in enumerate(zip(left, right)):
        if old == new:
            if start is not None:
                runs.append((start, previous + 1))
                start = None
            continue
        if start is None:
            start = address
        previous = address
    if start is not None:
        runs.append((start, previous + 1))

    def label(address):
        virtual = 0x80000000 | address
        for index, base, size in overlays:
            if base <= virtual < base + size:
                overlay_address = ((index + 0x10) << 20) | (virtual - base)
                if symbols:
                    return symbols.nearest(overlay_address)
                return f"overlay[{index}]+0x{virtual - base:x}"
        return symbols.nearest(virtual) if symbols else "<no map>"

    for start, end in runs[:limit]:
        changed = sum(a != b for a, b in zip(left[start:end], right[start:end]))
        print(f"0x{start:06x}-0x{end:06x} ({changed:4} bytes) {label(0x80000000 | start)}")
    if len(runs) > limit:
        print(f"... {len(runs) - limit} additional changed ranges omitted")
    print(f"{len(runs)} ranges, {sum(end-start for start,end in runs)} changed bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before")
    parser.add_argument("after")
    parser.add_argument("--limit", type=int, default=100)
    args = parser.parse_args()
    compare(args.before, args.after, args.limit)


if __name__ == "__main__":
    main()
