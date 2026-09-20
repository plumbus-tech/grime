#!/usr/bin/env python3
"""Regenerate configs/layouts/qwerty.json -- the identity layout.

It is an ordinary grime layer in an ordinary JSON file: every key on a standard
keyboard mapped to itself. `"base": "qwerty"` in a config means "fill in these
keys unless I said otherwise", which is how you get normal typing without
listing a hundred keys.

    scripts/gen-qwerty.py          # rewrite configs/layouts/qwerty.json
    scripts/gen-qwerty.py --list   # just print the key names
"""
import os
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

if "--list" in sys.argv:
    print("\n".join(k for row in ROWS for k in row.split()))
    sys.exit(0)

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "..", "configs", "layouts", "qwerty.json")
with open(out, "w") as f:
    f.write('{\n  "layers": {\n    "qwerty": {\n')
    body = []
    for row in ROWS:
        body.append("      " + " ".join('"%s": "%s",' % (k, k) for k in row.split()))
    f.write("\n".join(body).rstrip(",") + "\n")
    f.write("    }\n  }\n}\n")
n = sum(len(r.split()) for r in ROWS)
print("wrote %s (%d keys)" % (os.path.normpath(out), n), file=sys.stderr)
