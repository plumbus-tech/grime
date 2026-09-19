# Config

One JSON file. Default location `~/.config/grime/config.json`, or `grime -c PATH`.
Validate with `grime --check -c PATH`. Reload a running grime with `kill -HUP`
or a `{"do": "reload"}` binding — a broken config is rejected and the old one kept.

```json
{
  "devices": ["auto"],
  "keymap": { ... }
}
```

- `devices` — `["auto"]` (every keyboard-looking device, the default) or explicit
  paths like `"/dev/input/by-id/usb-Keychron-event-kbd"`.
- `keymap` — the root of the tree.

## Keymaps

A keymap is an object whose keys are key names (`grime --list-keys`; evdev names
lowercased: `a`, `1`, `leftshift`, `capslock`, `f13`, `kpenter`, …; aliases
`shift ctrl alt super`).

Each key maps to either a **shorthand string** or an object with a `press`
and/or `release` **binding**:

```json
"a": "a",                     // shorthand: press→press a, release→release a
"rightshift": "a",            // rightshift types a
"f1": { "press": { "do": "exec", "cmd": "firefox" } },
"capslock": {
  "press":   { "then": { ...a nested keymap... } },
  "release": { "alone": true, "do": "tap", "key": "esc" }
}
```

**A key (or edge) that isn't there does nothing.** Want normal typing? Put
every key in (see `scripts/gen-qwerty.py`, which prints the whole block).

## Bindings

| field         | meaning |
|---------------|---------|
| `do` + params | run an action (see below) |
| `then`        | a nested keymap: the path continues there |
| `exit`        | when to leave the nested keymap: `"release"` (default for press: when this key is released — hold/modifier style) `"action"` (after the first action fires inside it — Emacs prefix style; default for release bindings), or `"toggle"` (press only: stays on until this same binding is pressed again — see below) |
| `fallthrough` | keys not found in the nested keymap are looked up in the parent instead of doing nothing |
| `alone`       | release only: fire only if no other key was pressed while this one was held (tap-vs-hold) |

A binding can have both `do` and `then` (fire, then descend).

## How the walk works

1. A **press** is looked up in the current keymap (and parents, with `fallthrough`). No match → nothing (and an Emacs-style prefix is cancelled).
2. A **release** is looked up in the keymap where that key's **press** was handled — so press/release stay paired even if you changed layers in between.
3. `then` enters a nested keymap. `exit: release` layers are left when the key that entered them is released; `exit: action` layers are left (the whole prefix chain) once an action fires or on an unmatched key.
   `exit: toggle` layers stay on until the same binding is pressed again, wherever
   you reach it from. They survive everything else: turn one on from inside a
   hold layer or an Emacs prefix and it stays on after those end. Several can be
   on at once; the most recent is on top, and you can turn off any of them.
4. Key repeat events are ignored; the OS autorepeats held output keys itself.
5. There is no notion of time in the keymap. The walk is driven only by press and
   release events; "held" just means pressed and not yet released.

## Actions

| `do`      | params | |
|-----------|--------|---|
| `press`   | `key` | press a key (and keep it held) |
| `release` | `key` | release it |
| `tap`     | `key`, `mods` (array, optional) | press+release, with modifiers held around it: `{"do":"tap","key":"z","mods":["leftctrl"]}` |
| `type`    | `text` | type a string (US QWERTY, ASCII, `\n`, `\t`) |
| `exec`    | `cmd` (via `/bin/sh -c`) or `argv` (array) | run a command in the background, as you (not root) |
| `http`    | `url`, `method` (GET), `headers` (array), `body` (string or JSON), `timeout_ms` (10000) | async HTTP request; `${VAR}` expands from the environment when it fires |
| `seq`     | `actions` (array) | run several actions in order |
| `log`     | `msg` | print to grime's log |
| `reload`  | | re-read the config file |
| `quit`    | | exit and give the keyboard back |

`grime --list-actions` prints what your build has.

## Recipes

Hold-layer with tap: see `capslock` in `configs/default.json`.

Emacs prefix `rightalt x f`:
```json
"rightalt": { "press": { "exit": "action", "then": {
  "x": { "press": { "exit": "action", "then": {
    "f": { "press": { "do": "exec", "cmd": "xdg-open ~" } } } } } } } }
```

Override mode: take over some app shortcuts (Emacs, Firefox, Slack — grime sits
below all of them) and pass the rest through. `capslock o` switches it on and off:
```json
"o": { "press": { "exit": "toggle", "fallthrough": true,
  "do": "exec", "argv": ["notify-send", "grime", "override mode toggled"],
  "then": {
    "leftctrl": {
      "press": { "do": "press", "key": "leftctrl", "fallthrough": true, "then": {
        "f": { "press": { "do": "exec", "cmd": "firefox" } } } },
      "release": { "do": "release", "key": "leftctrl" } } } } }
```
- `fallthrough` on the toggle layer and on `leftctrl` sends every key you didn't
  override to the normal keymap, so `ctrl+s` still reaches the app as `ctrl+s`.
- Ctrl is still held while an override runs. That doesn't matter for
  `exec`/`http`, and a `tap` becomes another Ctrl chord (`ctrl+f` → `ctrl+l`).
- The toggle's `do` runs when it turns on and when it turns off.

Lights: `configs/examples/lights.json` (Home Assistant + Hue). Export
`HA_TOKEN` before starting grime.

Letters as modifiers / shift as a letter: `configs/examples/weird-modifiers.json`.
Note the classic home-row-mod caveat: with `a` as a hold-modifier, rolling `a`
into `s` quickly means "s inside a's layer", not "as".

## Safety
- Emergency exit: hold `Esc` + `Backspace` for 1 s (checked before the keymap, always works).
- `--timeout SEC` auto-exits. `--dry-run` logs without grabbing or emitting.
- grime waits for all keys to be released before grabbing.
