# grime

An Emacs-style keymap for your whole OS. grime grabs your physical keyboard and
walks every **press** and **release** through a tree you define in one JSON
file. Paths through the tree lead to **functions**: type a key, type text, run a
command, hit an HTTP API to toggle your lights, reload, quit.

- Any key can be a modifier (hold `capslock` → a layer; hold `a` → another).
- Any key can be anything (`rightshift` types `a`).
- Emacs-style sequences (`"rightalt x f"`) and chords (`"ctrl+f"`), written as you say them.
- Tap vs hold on one key: `{"tap": "esc", "hold": {...}}`.
- Sticky layers you toggle on and off — a second layout, or an override mode that
  outranks whatever app has focus.
- No clocks anywhere: the keymap is driven purely by press and release.
- No layouts baked in: QWERTY and Dvorak are JSON files under `configs/layouts/`,
  and so is yours.
- Mouse buttons are keys: bind `btn_middle`, or make `capslock`+click do something.
- Devices chosen by name, vendor:product or capability — not by an `eventN`
  number that moves between boots. `grime --list-devices` shows you which, and
  `grime --watch` shows you what they send. Plug one in and it's picked up.
- Fully async: one epoll loop; commands and HTTP calls never stall typing.

Linux first (evdev + uinput); the engine is OS-agnostic so other backends can follow.

## Try it

```sh
sudo apt install build-essential pkg-config libjson-c-dev libcurl4-openssl-dev   # once
scripts/grime-try.sh --dry-run          # doesn't touch your keyboard, logs what would happen
scripts/grime-try.sh                    # real thing, configs/default.json, auto-exits after 60 s
scripts/grime-try.sh configs/examples/weird-modifiers.json --timeout 120
```

**Get out:** hold `Esc` + `Backspace`, or all three mouse buttons, for 1 second. The try script also
auto-exits after 60 s by default (`--timeout 0` to disable).

In `configs/default.json` (44 lines — it includes `layouts/qwerty.json` for
normal typing), plus
- `capslock` tap → Esc; hold `capslock` + `h j k l` → arrows, `+u` → Ctrl-Z,
  `+t` → notification, `+y` → types text, `+r` → reload config, `+q` → quit
- `capslock o` → turn override mode on/off: `ctrl+f` opens Firefox and `ctrl+t` sends a
  notification instead of reaching the app; every other ctrl chord passes through
- `capslock d` → toggle Dvorak on and off, without touching your OS layout
- `rightalt x t` → notification (Emacs-style prefix; any undefined key cancels it)
- `tab` tap → Tab; hold `tab` + `f` → Firefox, `+c` → caps lock

`scripts/setup-permissions.sh` (once, then re-login) lets you run without sudo.

## Docs
- [docs/CONFIG.md](docs/CONFIG.md) — the config format and every action
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the pieces fit
- [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) — how two of us work on it without collisions

## Build
```sh
make            # build/grime
make test       # engine + config tests, no root needed
make check-configs
build/grime --expand -c configs/default.json   # what your config really means
build/grime --list-devices                    # what grime can see, and what it would take
build/grime --watch                           # what your keys send (never grabs)
build/grime --help
```
