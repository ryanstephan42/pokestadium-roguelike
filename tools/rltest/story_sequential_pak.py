"""Prepare a fresh isolated single-bank Pak from a validated HOST story note.

Refuses nonempty directories, nonfree data pages and overwritten outputs.
This converts a supplied formatted template, not a user's existing Pak.
No emulator, savestate or live save-memory write is performed.
"""

import argparse
import hashlib
import json
import pathlib

from tools.rltest.frontend import HarnessError
from tools.rltest.save_roundtrip import find_pak, checksum_inode
from tools.rltest.story_probe import story_owned_changes

NAME = bytes([44, 45, 26, 29, 34, 46, 38, 15, 44, 45, 40, 43, 50, 0, 0, 0])
EXTENSION = bytes([47, 17, 0, 0])


def sha(data):
    return hashlib.sha256(data).hexdigest()


def convert(template, note, expected_note_sha):
    if sha(note) != expected_note_sha or len(note) != 1536:
        raise HarnessError("Story note length/validated package hash mismatch")
    if len(template) != 32768 or find_pak(template) != 0:
        raise HarnessError("Use a formatted isolated 32 KiB single-bank Pak template")
    if any(template[768:1280]):
        raise HarnessError("Pak template has existing notes; refusing non-isolated conversion")
    inode = template[256:512]
    if inode != template[512:768] or inode[1] != sum(inode[10:]) & 255:
        raise HarnessError("Pak inode mirror/checksum mismatch")
    if any(inode[page * 2:page * 2 + 2] != b"\x00\x03" for page in range(5, 128)):
        raise HarnessError("Pak template has allocated/orphan data pages")
    # A first journal has only slot0 committed by sc_storage_store; slot1 is empty.
    if note[:4] != b"SC01" or any(note[736:]):
        raise HarnessError("Expected validated first-slot-only story journal")
    result = bytearray(template)
    pages = list(range(5, 11))
    directory = bytearray(32)
    directory[:6] = b"SC01RL"
    directory[6:8] = pages[0].to_bytes(2, "big")
    directory[8] = 2
    directory[12:16] = EXTENSION
    directory[16:32] = NAME
    result[768:800] = directory
    for i, page in enumerate(pages):
        following = pages[i + 1] if i + 1 < len(pages) else 1
        result[256 + page * 2:258 + page * 2] = following.to_bytes(2, "big")
        result[page * 256:(page + 1) * 256] = note[i * 256:(i + 1) * 256]
    checksum_inode(result, 0)
    if story_owned_changes(template, result, 0) != pages:
        raise HarnessError("Pak conversion changed unowned storage")
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=pathlib.Path, required=True)
    parser.add_argument("--package", type=pathlib.Path, required=True)
    parser.add_argument("--note", choices=("normal-route.note", "normal-interrupted.note",
                                          "transform-route.note"), required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    manifest_raw = (args.package / "manifest.json").read_bytes()
    if sha(manifest_raw) != (args.package / "manifest.sha256").read_text().strip():
        raise HarnessError("Source package manifest hash mismatch")
    manifest = json.loads(manifest_raw)
    template = args.template.read_bytes()
    note = (args.package / args.note).read_bytes()
    result = convert(template, note, manifest["files"][args.note])
    with args.out.open("xb") as output:
        output.write(result)
    print(json.dumps(dict(scope="HOST conversion; native Pak acceptance unverified",
                         template_sha256=sha(template), note_sha256=sha(note),
                         pak_sha256=sha(result), note=args.note), indent=2))


if __name__ == "__main__":
    main()
