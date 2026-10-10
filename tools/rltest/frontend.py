"""Minimal, synchronous libretro frontend for headless N64 testing."""

from __future__ import annotations

import ctypes as ct
import hashlib
import json
import os
import re
import struct
import sys
import time
import zlib
from bisect import bisect_right
from pathlib import Path


class HarnessError(RuntimeError):
    pass


class Variable(ct.Structure):
    _fields_ = [("key", ct.c_char_p), ("value", ct.c_char_p)]


class GameInfo(ct.Structure):
    _fields_ = [("path", ct.c_char_p), ("data", ct.c_void_p),
                ("size", ct.c_size_t), ("meta", ct.c_char_p)]


class SystemInfo(ct.Structure):
    _fields_ = [("library_name", ct.c_char_p), ("library_version", ct.c_char_p),
                ("valid_extensions", ct.c_char_p), ("need_fullpath", ct.c_bool),
                ("block_extract", ct.c_bool)]


Env = ct.CFUNCTYPE(ct.c_bool, ct.c_uint, ct.c_void_p)
Video = ct.CFUNCTYPE(None, ct.c_void_p, ct.c_uint, ct.c_uint, ct.c_size_t)
Audio = ct.CFUNCTYPE(None, ct.c_int16, ct.c_int16)
AudioBatch = ct.CFUNCTYPE(ct.c_size_t, ct.POINTER(ct.c_int16), ct.c_size_t)
InputPoll = ct.CFUNCTYPE(None)
InputState = ct.CFUNCTYPE(ct.c_int16, ct.c_uint, ct.c_uint, ct.c_uint, ct.c_uint)
LogPrint = ct.CFUNCTYPE(None, ct.c_int, ct.c_char_p)


class LogInterface(ct.Structure):
    _fields_ = [("log", LogPrint)]

# RETRO_DEVICE_ID_JOYPAD_* indices used by the core.
# Names A/B/Y match libretro joypad IDs (not the N64 physical button labels).
# On the installed Mupen core, RetroPad B (0) maps to N64 A and RetroPad Y (1)
# maps to N64 B. N64_A/N64_B aliases make game-intent scenarios explicit.
BUTTONS = {"B": 0, "Y": 1, "SELECT": 2, "START": 3, "UP": 4, "DOWN": 5,
           "LEFT": 6, "RIGHT": 7, "A": 8, "X": 9, "L": 10, "R": 11,
           "Z": 12, "N64_A": 0, "N64_B": 1}
SOFTWARE_OPTIONS = {
    "parallel-n64-rdp-plugin": "angrylion",
    "parallel-n64-rsp-plugin": "cxd4",
    "parallel-n64-cpucore": "dynamic_recompiler",
    "mupen64plus-rdp-plugin": "angrylion",
    "mupen64plus-rsp-plugin": "hle",
    "mupen64plus-cpucore": "dynamic_recompiler",
}


class Symbols:
    """GNU ld symbol lookup, including nearest-symbol offset reporting."""

    _line = re.compile(r"^\s*0x([0-9a-fA-F]+)\s+([A-Za-z_]\w*)\s*$")

    def __init__(self, map_path):
        self.path = Path(map_path)
        self.addresses = {}
        for line in self.path.read_text().splitlines():
            match = self._line.match(line)
            if match:
                self.addresses[match.group(2)] = int(match.group(1), 16)
        if not self.addresses:
            raise HarnessError(f"No symbols found in {self.path}")
        self.ordered = sorted((addr, name) for name, addr in self.addresses.items())
        self.keys = [item[0] for item in self.ordered]

    def address(self, symbol):
        try:
            return self.addresses[symbol]
        except KeyError as exc:
            raise HarnessError(f"Symbol {symbol!r} is absent from {self.path}") from exc

    def nearest(self, address):
        index = bisect_right(self.keys, address) - 1
        if index < 0:
            return "<no symbol>"
        base, name = self.ordered[index]
        return f"{name}+0x{address - base:x}"


def _png(data, width, height, pitch, fmt):
    bpp = 4 if fmt == 1 else 2
    if fmt not in (0, 1, 2) or width <= 0 or height <= 0 or pitch < width * bpp:
        raise HarnessError("Unsupported pixel format or framebuffer geometry")
    if len(data) < height * pitch:
        raise HarnessError("Incomplete framebuffer data")
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            start = y * pitch + x * bpp
            pix = int.from_bytes(data[start:start + bpp], sys.byteorder)
            if fmt == 1:  # XRGB8888
                rgb = ((pix >> 16) & 255, (pix >> 8) & 255, pix & 255)
            else:  # RGB1555 or RGB565
                green_bits = 6 if fmt == 2 else 5
                r = (pix >> (green_bits + 5)) & 31
                g = (pix >> 5) & ((1 << green_bits) - 1)
                b = pix & 31
                rgb = (r * 255 // 31, g * 255 // ((1 << green_bits) - 1), b * 255 // 31)
            rows.extend(rgb)

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body)))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class Frontend:
    """One core instance. Controllers are 1 and 2; RAM access is N64 big-endian.

    Supported N64 cores expose word-swapped RDRAM on little-endian systems by
    default; set ram_order="big" for cores configured to expose canonical bytes.
    """

    def __init__(self, core, rom, map_path, output=".local/rltest", *, options=None,
                 ram_order="word-swapped", timeout=110):
        self.core_path = Path(core).expanduser().resolve()
        self.rom_path = Path(rom).resolve()
        self.symbols = Symbols(map_path)
        self.output = Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.options = {**SOFTWARE_OPTIONS, **(options or {})}
        if ram_order not in ("word-swapped", "big"):
            raise ValueError("ram_order must be 'word-swapped' or 'big'")
        self.ram_order = ram_order
        if not self.core_path.is_file() or not self.rom_path.is_file():
            raise HarnessError("Core or ROM file does not exist")
        self.frame = 0
        self.held = [set(), set()]
        self.analog = [[0, 0, 0, 0], [0, 0, 0, 0]]
        self.pixel_format = 0
        self.framebuffer = None
        self.video_frames = 0
        self.callback_error = None
        self.hardware_requested = False
        self.shutdown = False
        self.initialized = self.loaded = False
        self.deadline = time.monotonic() + timeout
        self._strings = {}
        self._rom_data = None
        self._log = LogPrint(self._log_message)
        self._log_interface = LogInterface(self._log)
        self.core = ct.CDLL(str(self.core_path))
        self._bind()
        self.callbacks = (
            Env(self._environment), Video(self._video),
            Audio(lambda left, right: None), AudioBatch(lambda data, frames: frames),
            InputPoll(lambda: None), InputState(self._input),
        )
        try:
            debug = "RLTEST_DEBUG" in os.environ
            for name, callback in zip(
                ("environment", "video_refresh", "audio_sample", "audio_sample_batch",
                 "input_poll", "input_state"), self.callbacks
            ):
                getattr(self.core, f"retro_set_{name}")(callback)
            if debug:
                print("[rltest] callbacks installed", file=sys.stderr)
            if self.core.retro_api_version() != 1:
                raise HarnessError("Unsupported libretro API version")
            self.core.retro_init()
            self.initialized = True
            if debug:
                print("[rltest] core initialized", file=sys.stderr)
            info = SystemInfo()
            self.core.retro_get_system_info(ct.byref(info))
            self.library = (info.library_name or b"unknown").decode()
            if debug:
                print(f"[rltest] system info: {self.library}, fullpath={info.need_fullpath}, "
                      f"extensions={info.valid_extensions!r}", file=sys.stderr)
            game = GameInfo(str(self.rom_path).encode(), None, 0, None)
            if not info.need_fullpath:
                self._rom_data = ct.create_string_buffer(self.rom_path.read_bytes())
                game.data = ct.cast(self._rom_data, ct.c_void_p)
                game.size = ct.sizeof(self._rom_data) - 1
            self.loaded = bool(self.core.retro_load_game(ct.byref(game)))
            if debug:
                print(f"[rltest] retro_load_game={self.loaded}", file=sys.stderr)
            self._check()
            if not self.loaded:
                raise HarnessError(f"{self.library} refused {self.rom_path}")
            for port in (0, 1):
                self.core.retro_set_controller_port_device(port, 1)
            # N64 cores allocate/expose RDRAM during the first frame, not ROM load.
            self.step(1)
        except BaseException:
            self.close()
            raise

    def _bind(self):
        signatures = {
            "retro_api_version": ([], ct.c_uint),
            "retro_init": ([], None), "retro_deinit": ([], None),
            "retro_run": ([], None), "retro_unload_game": ([], None),
            "retro_get_system_info": ([ct.POINTER(SystemInfo)], None),
            "retro_load_game": ([ct.POINTER(GameInfo)], ct.c_bool),
            "retro_set_controller_port_device": ([ct.c_uint, ct.c_uint], None),
            "retro_get_memory_data": ([ct.c_uint], ct.c_void_p),
            "retro_get_memory_size": ([ct.c_uint], ct.c_size_t),
            "retro_serialize_size": ([], ct.c_size_t),
            "retro_serialize": ([ct.c_void_p, ct.c_size_t], ct.c_bool),
            "retro_unserialize": ([ct.c_void_p, ct.c_size_t], ct.c_bool),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.core, name)
            fn.argtypes, fn.restype = args, result
        for name, callback in (
            ("environment", Env), ("video_refresh", Video), ("audio_sample", Audio),
            ("audio_sample_batch", AudioBatch), ("input_poll", InputPoll),
            ("input_state", InputState),
        ):
            fn = getattr(self.core, f"retro_set_{name}")
            fn.argtypes, fn.restype = [callback], None

    def _string(self, value):
        return self._strings.setdefault(value, value.encode())

    def _environment(self, command, data):
        # Never allow Python exceptions to cross a native callback boundary.
        try:
            if command == 3:  # GET_CAN_DUPE
                ct.cast(data, ct.POINTER(ct.c_bool))[0] = True
            elif command in (9, 30, 31):  # system, assets, save directories
                ct.cast(data, ct.POINTER(ct.c_char_p))[0] = self._string(str(self.output))
            elif command == 10:  # SET_PIXEL_FORMAT
                value = ct.cast(data, ct.POINTER(ct.c_int))[0]
                if value not in (0, 1, 2):
                    return False
                self.pixel_format = value
            elif command == 14:  # SET_HW_RENDER: no hidden GL context
                if "RLTEST_DEBUG" in os.environ:
                    print(f"[rltest] core requested hardware renderer; options={self.options}", file=sys.stderr)
                self.hardware_requested = True
                return False
            elif command == 15:  # GET_VARIABLE
                variable = ct.cast(data, ct.POINTER(Variable)).contents
                key = variable.key.decode()
                if key not in self.options:
                    variable.value = None
                    return False
                variable.value = self._string(self.options[key])
            elif command == 16:  # legacy core option definitions
                variables = ct.cast(data, ct.POINTER(Variable))
                index = 0
                while variables[index].key:
                    key = variables[index].key.decode()
                    choices = variables[index].value.decode().split(";", 1)[1].strip().split("|")
                    if key in self.options and self.options[key] not in choices:
                        raise HarnessError(f"Unsupported core option {key}={self.options[key]}")
                    self.options.setdefault(key, choices[0])
                    if "RLTEST_DEBUG" in os.environ:
                        print(f"[rltest] {key}={self.options[key]} choices={choices}", file=sys.stderr)
                    index += 1
            elif command == 17:  # GET_VARIABLE_UPDATE
                ct.cast(data, ct.POINTER(ct.c_bool))[0] = False
            elif command == 27:  # GET_LOG_INTERFACE
                ct.cast(data, ct.POINTER(LogInterface))[0] = self._log_interface
            elif command == 47:  # GET_AUDIO_VIDEO_ENABLE
                ct.cast(data, ct.POINTER(ct.c_int))[0] = 1
            elif command == 51:  # GET_CORE_OPTIONS_VERSION
                ct.cast(data, ct.POINTER(ct.c_uint))[0] = 0
            elif command == 39:  # GET_LANGUAGE
                ct.cast(data, ct.POINTER(ct.c_uint))[0] = 0
            elif command == 7:  # SHUTDOWN
                self.shutdown = True
            elif command == 8:  # SET_PERFORMANCE_LEVEL
                pass
            elif command == 6:  # SET_MESSAGE
                message = ct.cast(data, ct.POINTER(ct.c_char_p))[0]
                print(f"[core] {(message or b'').decode(errors='replace')}", file=sys.stderr)
            elif command in (11, 18, 35, 37):  # descriptors, no-game, controllers, geometry
                pass
            else:
                return False
            return True
        except Exception as exc:
            self.callback_error = exc
            return False

    @staticmethod
    def _log_message(level, message):
        if message and level >= 2:
            print(f"[libretro:{level}] {message.decode(errors='replace').rstrip()}", file=sys.stderr)

    def _video(self, data, width, height, pitch):
        try:
            if not data:  # duplicate last frame
                return
            if data == ct.c_void_p(-1).value:
                raise HarnessError("Hardware framebuffer provided without a GL context")
            if width > 4096 or height > 4096 or pitch > 65536:
                raise HarnessError("Invalid core framebuffer dimensions")
            self.framebuffer = (ct.string_at(data, height * pitch), width, height, pitch)
            self.video_frames += 1
        except Exception as exc:
            self.callback_error = exc

    def _input(self, port, device, index, button):
        if port >= 2:
            return 0
        if device == 1 and index == 0:
            return int(button in self.held[port])
        if device == 5 and index < 2 and button < 2:
            return self.analog[port][index * 2 + button]
        return 0

    def _check(self):
        if self.callback_error:
            raise HarnessError(f"Native callback failed: {self.callback_error}") from self.callback_error
        if self.hardware_requested:
            raise HarnessError("Core requires hardware video; configure angrylion software rendering")
        if self.shutdown:
            raise HarnessError("Core requested shutdown")
        if time.monotonic() >= self.deadline:
            raise HarnessError(f"Emulator timed out at frame {self.frame}")

    def step(self, frames=1):
        if not isinstance(frames, int) or frames < 0:
            raise ValueError("frames must be a non-negative integer")
        for _ in range(frames):
            self._check()
            self.core.retro_run()
            self.frame += 1
            self._check()

    @staticmethod
    def _port(controller):
        if controller not in (1, 2):
            raise ValueError("controller must be 1 or 2")
        return controller - 1

    def hold(self, *buttons, controller=1):
        self.held[self._port(controller)].update(BUTTONS[b.upper()] for b in buttons)

    def release(self, *buttons, controller=1):
        held = self.held[self._port(controller)]
        if buttons:
            held.difference_update(BUTTONS[b.upper()] for b in buttons)
        else:
            held.clear()

    def press(self, *buttons, controller=1, frames=2, after=2):
        port = self._port(controller)
        previous = self.held[port].copy()
        self.hold(*buttons, controller=controller)
        try:
            self.step(frames)
        finally:
            self.held[port] = previous
        self.step(after)

    def stick(self, x=0, y=0, *, controller=1, right=False):
        if not (-32768 <= x <= 32767 and -32768 <= y <= 32767):
            raise ValueError("Analog values must be int16")
        offset = 2 if right else 0
        self.analog[self._port(controller)][offset:offset + 2] = [x, y]

    def _ram(self):
        pointer = self.core.retro_get_memory_data(2)  # RETRO_MEMORY_SYSTEM_RAM
        size = self.core.retro_get_memory_size(2)
        if not pointer or size < 4:
            raise HarnessError("Core did not expose RDRAM")
        return (ct.c_ubyte * size).from_address(pointer)

    @staticmethod
    def _physical(address):
        if 0x80000000 <= address < 0x81000000 or 0xA0000000 <= address < 0xA1000000:
            return address & 0x1FFFFFFF
        if 0 <= address < 0x1000000:
            return address
        raise HarnessError(f"Not a direct RDRAM address: {address:#x}")

    def resolve(self, address):
        if isinstance(address, str):
            address = self.symbols.address(address)
        if 0x81000000 <= address < 0x90000000:
            index = ((address & 0x0FF00000) >> 20) - 0x10
            entry = self.symbols.address("gFragments") + index * 8
            base, size = self.read_u32(entry), self.read_u32(entry + 4)
            offset = address & 0xFFFFF
            if not base or offset >= size:
                raise HarnessError(f"Overlay address {address:#x} is not currently loaded")
            return base + offset
        return address

    def read(self, address, size):
        offset = self._physical(self.resolve(address))
        ram = self._ram()
        if size < 0 or offset + size > len(ram):
            raise HarnessError("RDRAM read out of range")
        xor = 3 if self.ram_order == "word-swapped" else 0
        return bytes(ram[(offset + i) ^ xor] for i in range(size))

    def write(self, address, data):
        offset = self._physical(self.resolve(address))
        ram = self._ram()
        if offset + len(data) > len(ram):
            raise HarnessError("RDRAM write out of range")
        xor = 3 if self.ram_order == "word-swapped" else 0
        for i, value in enumerate(data):
            ram[(offset + i) ^ xor] = value

    def read_u8(self, address):
        return self.read(address, 1)[0]

    def read_u16(self, address):
        return int.from_bytes(self.read(address, 2), "big")

    def read_u32(self, address):
        return int.from_bytes(self.read(address, 4), "big")

    def write_u16(self, address, value):
        self.write(address, value.to_bytes(2, "big"))

    def write_u32(self, address, value):
        self.write(address, value.to_bytes(4, "big"))

    def wait(self, predicate, *, frames=1800, interval=1, description="condition"):
        end = self.frame + frames
        while not predicate(self):
            if self.frame >= end:
                raise HarnessError(f"Timed out waiting for {description}")
            self.step(min(interval, end - self.frame))

    def wait_state(self, state, frames=1800):
        self.wait(lambda emu: emu.read_u32("gCurrentGameState") == state,
                  frames=frames, description=f"gCurrentGameState={state:#x}")

    def battle_mons(self):
        """Return live fragment-62 side pointers and active mon HP/PP, or None."""
        try:
            sides = self.resolve("D_84390010")
            result = []
            for side_index in range(2):
                side = self.read_u32(sides + side_index * 4)
                if side < 0x80000000 or side >= 0x80800000:
                    return None
                mon = side + 0x68C
                species = self.read_u8(mon + 0x0B)
                hp = self.read_u16(mon + 0x0C)
                status = self.read_u8(mon + 0x15)
                level = self.read_u8(mon + 0x26)
                max_hp = self.read_u16(mon + 0x28)
                pp = self.read(mon + 0x32, 4)
                if species == 0 or max_hp == 0 or hp > max_hp:
                    return None
                result.append({
                    "side": side, "species": species, "hp": hp, "max_hp": max_hp,
                    "status": status, "level": level,
                    "moves": list(self.read(mon + 0x1F, 4)),
                    "pp": [value & 0x3F for value in pp],
                })
            return result
        except HarnessError:
            return None

    def wait_battle(self, frames=60 * 60):
        """Wait for both fragment-62 active battle mons to be initialized."""
        self.wait(lambda emu: emu.battle_mons() is not None,
                  frames=frames, description="initialized fragment-62 battle mons")
        return self.battle_mons()

    def screenshot(self, path):
        if self.framebuffer is None:
            raise HarnessError("No software framebuffer captured")
        data, width, height, pitch = self.framebuffer
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(_png(data, width, height, pitch, self.pixel_format))

    def snapshot(self, path):
        data = bytes(self._ram())
        if self.ram_order == "word-swapped":
            canonical = bytearray(len(data))
            for i in range(4):
                canonical[i::4] = data[3 - i::4]
            data = bytes(canonical)
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        overlays = []
        fragment_table = self.symbols.addresses.get("gFragments")
        if fragment_table is not None:
            off = self._physical(fragment_table)
            for i in range(240):
                base, size = struct.unpack_from(">II", data, off + i * 8)
                if base and size:
                    overlays.append([i, base, size])
        path.with_suffix(path.suffix + ".json").write_text(json.dumps({
            "frame": self.frame, "map": str(self.symbols.path.resolve()),
            "overlays": overlays,
        }, indent=2) + "\n")

    def _identity(self):
        return {
            "rom_sha256": hashlib.sha256(self.rom_path.read_bytes()).hexdigest(),
            "core_sha256": hashlib.sha256(self.core_path.read_bytes()).hexdigest(),
            "options": self.options, "ram_order": self.ram_order,
        }

    def save(self, path):
        size = self.core.retro_serialize_size()
        if not size:
            raise HarnessError("Core does not support savestates")
        data = ct.create_string_buffer(size)
        if not self.core.retro_serialize(data, size):
            raise HarnessError("Savestate serialization failed")
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.raw)
        path.with_suffix(path.suffix + ".json").write_text(json.dumps({
            **self._identity(), "frame": self.frame,
        }, indent=2) + "\n")

    def load(self, path):
        path = Path(path)
        metadata = json.loads(path.with_suffix(path.suffix + ".json").read_text())
        if any(metadata.get(key) != value for key, value in self._identity().items()):
            raise HarnessError("Savestate ROM/core/options/byte-order mismatch")
        data = path.read_bytes()
        buffer = ct.create_string_buffer(data)
        if not self.core.retro_unserialize(buffer, len(data)):
            raise HarnessError("Core rejected savestate")
        self.frame = metadata["frame"]
        self.held = [set(), set()]
        self.analog = [[0, 0, 0, 0], [0, 0, 0, 0]]
        self.framebuffer = None

    def close(self):
        if self.loaded:
            self.core.retro_unload_game()
            self.loaded = False
        if self.initialized:
            self.core.retro_deinit()
            self.initialized = False

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
