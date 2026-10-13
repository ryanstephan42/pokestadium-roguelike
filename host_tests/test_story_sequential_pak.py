import hashlib
import struct
import unittest

from tools.rltest.frontend import HarnessError
from tools.rltest.save_roundtrip import checksum_inode
from tools.rltest.story_sequential_pak import convert, NAME, EXTENSION


def template():
    image = bytearray(32768)
    header = bytearray(32)
    header[24:28] = b"\x00\x01\x01\x00"
    words = struct.unpack(">14H", header[:28])
    header[28:30] = (sum(words) & 65535).to_bytes(2, "big")
    header[30:32] = (sum((~x) & 65535 for x in words) & 65535).to_bytes(2, "big")
    for block in (1, 3, 4, 6):
        image[block * 32:(block + 1) * 32] = header
    for page in range(5, 128):
        image[256 + page * 2:258 + page * 2] = b"\x00\x03"
    checksum_inode(image, 0)
    return image


class SequentialPakTests(unittest.TestCase):
    def setUp(self):
        self.note = b"SC01" + bytes(1532)
        self.hash = hashlib.sha256(self.note).hexdigest()

    def test_private_note_directory_chain_and_payload(self):
        original = template()
        result = convert(original, self.note, self.hash)
        self.assertEqual(result[768:776], b"SC01RL\x00\x05")
        self.assertEqual(result[776], 2)
        self.assertEqual(result[780:784], EXTENSION)
        self.assertEqual(result[784:800], NAME)
        self.assertEqual(result[1280:2816], self.note)
        self.assertEqual(result[256:512], result[512:768])
        self.assertEqual([int.from_bytes(result[256 + i * 2:258 + i * 2], "big")
                          for i in range(5, 11)], [6, 7, 8, 9, 10, 1])
        self.assertEqual(original, template())

    def test_refuses_existing_notes_or_orphan_pages(self):
        for offset in (768, 800, 256 + 40 * 2):
            image = template()
            image[offset] = 1
            if offset < 512:
                checksum_inode(image, 0)
            with self.assertRaises(HarnessError):
                convert(image, self.note, self.hash)

    def test_refuses_hash_shape_or_inode_damage(self):
        with self.assertRaises(HarnessError):
            convert(template(), self.note, "0" * 64)
        for raw in (self.note[:-1], self.note[:768] + b"\1" + self.note[769:]):
            with self.assertRaises(HarnessError):
                convert(template(), raw, hashlib.sha256(raw).hexdigest())
        image = template(); image[513] ^= 1
        with self.assertRaises(HarnessError):
            convert(image, self.note, self.hash)


if __name__ == "__main__":
    unittest.main()
