"""Public synthetic pin layout (must match out_sms_gate_pin.h placeholders)."""
from __future__ import annotations

IMAGE_BASE = 0x100000000
TEXT_VA = 0x100050000
TEXT_SIZE = 0x60000
OSLOG_VA = 0x1000D0000
OSLOG_SIZE = 0x10000
GATE_VA = 0x1000A0000
GATE_U32 = 0x34000080
BLOCK_HEAD = 0x1000A0010
ADRP_VA = 0x1000A0014
STR_VA = 0x1000D0580
COMM_SIZE = 0x00100000
TOKEN = b"#I Sms not allowed on device.\x00"
TOKEN_WRONG_OFFSET3 = b"I Sms not allowed on device.\x00"
SYNTH_UUID = bytes([0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0])