#!/usr/bin/env python3
"""Pad enlarged roguelike ROMs to the next supported N64 size."""

import argparse
import os


THIRTY_TWO_MIB = 32 * 1024 * 1024
SIXTY_FOUR_MIB = 64 * 1024 * 1024


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom")
    args = parser.parse_args()

    size = os.path.getsize(args.rom)
    if size <= THIRTY_TWO_MIB:
        return
    if size > SIXTY_FOUR_MIB:
        parser.error(f"ROM is larger than 64 MiB: {size} bytes")
    with open(args.rom, "r+b") as rom:
        rom.truncate(SIXTY_FOUR_MIB)
    print(f"Padded RL ROM from {size} bytes to {SIXTY_FOUR_MIB} bytes")


if __name__ == "__main__":
    main()
