#!/usr/bin/env python3
"""Print a plain-typing QWERTY keymap ("a": "a" for every key on a standard
keyboard) as JSON. Paste it into a config, or pipe it:

    scripts/gen-qwerty.py > my-keymap.json

Remember: in grime a key that isn't in the keymap does nothing, so this is the
starting point for "my keyboard works normally, plus extras"."""
import json
import sys

ROWS = [
    "esc f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12 sysrq scrolllock pause",
    "grave 1 2 3 4 5 6 7 8 9 0 minus equal backspace insert home pageup",
    "tab q w e r t y u i o p leftbrace rightbrace backslash delete end pagedown",
    "capslock a s d f g h j k l semicolon apostrophe enter",
    "leftshift z x c v b n m comma dot slash rightshift up",
    "leftctrl leftmeta leftalt space rightalt rightmeta compose rightctrl left down right",
    "numlock kpslash kpasterisk kpminus kp7 kp8 kp9 kpplus kp4 kp5 kp6 kp1 kp2 kp3 kpenter kp0 kpdot",
    "mute volumedown volumeup playpause nextsong previoussong brightnessdown brightnessup",
]

keys = [k for row in ROWS for k in row.split()]
skip = set(sys.argv[1:])  # e.g. gen-qwerty.py capslock  -> leave capslock out
json.dump({k: k for k in keys if k not in skip}, sys.stdout, indent=2)
print()
