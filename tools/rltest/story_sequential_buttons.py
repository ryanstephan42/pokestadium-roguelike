"""Source-derived native menu policy; no assumed libretro C-button mapping."""

from tools.rltest.frontend import HarnessError

MOVE_BUTTON = ("C_UP", "C_RIGHT", "C_DOWN", "C_LEFT")
PARTY_SIX_BUTTON = ("C_UP", "C_LEFT", "N64_B", "C_RIGHT", "C_DOWN", "N64_A")
MASKS = dict(C_UP=8, C_RIGHT=1, C_DOWN=4, C_LEFT=2, N64_A=0x8000,
             N64_B=0x4000, L=0x20, START=0x1000)


def read_menu(emu, run_address):
    """Read only during the bound sequential BATL stage; source offsets below.

    fragment62.h: side+654 controls, side+68C mon, side+720 setup,
    side+8C4 input lock. 29BA0.h: setup+8 teams; team+1 controller,
    team+2 count, team+1C six records of 54 bytes, HP at record+2.
    Final ELF/overlay binding and native menu timing are still required.
    """
    if emu.read_u32(run_address) != 4 or emu.read_u32("gScCampaignBattle") != 1:
        raise HarnessError("Battle menu unavailable outside sequential BATL lease")
    side = emu.read_u32("D_84390010")
    def pointer(value, size):
        if not 0x80000000 <= value <= 0x80800000 - size:
            raise HarnessError("Native menu pointer outside 8 MiB RDRAM")
        return value
    pointer(side, 0x950)
    controls = side + 0x654
    setup = pointer(emu.read_u32(side + 0x720), 0x10)
    # One human controller and one party, not multi-controller Stadium teams.
    if emu.read_u8(setup + 1) != 1:
        raise HarnessError("Sequential policy only supports one trainer per side")
    team = pointer(emu.read_u32(setup + 8), 0x21C)
    if emu.read_u8(team + 1) != 0:
        raise HarnessError("Sequential policy requires native controller 1")
    count = emu.read_u8(team + 2)
    if not 1 <= count <= 6:
        raise HarnessError("Native menu party count invalid")
    encoded_disable = emu.read_u8(side + 0x68C + 0x52)
    return dict(action=emu.read_u32(controls + 4), mode=emu.read_u32(controls + 0x10),
                locked=bool(emu.read_u32(side + 0x8C4)),
                active=emu.read_u32(controls + 8),
                selected_move=emu.read_u32(controls + 0x14),
                selected_party=emu.read_u32(controls + 0x18),
                moves=list(emu.read(side + 0x68C + 0x1F, 4)),
                pp=list(emu.read(side + 0x68C + 0x32, 4)),
                disabled=encoded_disable >> 4 if encoded_disable & 15 else -1,
                count=count, hp=[emu.read_u16(team + 0x1C + i * 0x54 + 2)
                                 for i in range(count)])


def accepted(menu, kind, slot):
    if kind not in ("move", "party"):
        raise HarnessError("Unknown native action acknowledgement")
    return menu["mode"] == 3 and menu["selected_" + kind] == slot


def choose_move(menu, slot):
    # func_8430EF4C and EE50: unk_04==1, unk_10==1, no auto lock.
    if (menu["action"], menu["mode"], menu["locked"]) != (1, 1, False):
        raise HarnessError("Move input before actual unlocked move-menu boundary")
    if not 0 <= slot < 4 or not 0 < menu["moves"][slot] < 166:
        raise HarnessError("Move slot has no legal move")
    if not menu["pp"][slot] & 0x3F or menu["disabled"] == slot:
        raise HarnessError("Move slot unavailable by native PP/disable guard")
    return MOVE_BUTTON[slot]


def choose_party(menu, slot):
    # E72C/E8E8: actual native index, not story order; use launcher mapping.
    count = menu["count"]
    if menu["mode"] != 2 or menu["locked"] or not 1 <= count <= 6:
        raise HarnessError("Party input before actual unlocked party-menu boundary")
    if not 0 <= slot < count or not menu["hp"][slot] or slot == menu["active"]:
        raise HarnessError("Party slot absent, fainted or already active")
    return PARTY_SIX_BUTTON[slot] if count >= 4 else MOVE_BUTTON[slot]


def action_menu(action):
    # F0C0: A -> fight, B -> switch, START -> third native action.
    if action not in ("fight", "switch"):
        raise HarnessError("Proposal never authorizes the third action/forfeit")
    return "N64_A" if action == "fight" else "N64_B"


def press_verified(emu, button, calibration, step):
    """Calibration must be observed from this exact core, not guessed aliases.

    step is the supervisor's counted/acknowledged frame callable. Core-specific
    input binding remains an emulator-only check before scenario inputs.
    """
    binding = calibration.get(button)
    if binding is None:
        raise HarnessError(f"Unverified core input mapping: {button}")
    if any(emu.held) or any(value for port in emu.analog for value in port):
        raise HarnessError("Edge input requires all prior buttons released")
    if binding["kind"] == "joypad" and type(binding["id"]) is int and 0 <= binding["id"] <= 15:
        emu.held[0].add(binding["id"])
        try:
            step(6)
        finally:
            emu.held[0].clear()
    elif binding["kind"] == "right-stick" and binding["xy"] in (
            [0, 32767], [0, -32768], [32767, 0], [-32768, 0]):
        emu.stick(*binding["xy"], right=True)
        try:
            step(6)
        finally:
            emu.stick(0, 0, right=True)
    else:
        raise HarnessError("Unsupported core-specific button calibration")
    step(2)
