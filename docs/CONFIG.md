# Config

One JSON file. Default location `~/.config/grime/config.json`, or `grime -c PATH`.
Validate with `grime --check -c PATH`. See what it really means with
`grime --expand -c PATH`. Reload a running grime with `kill -HUP` or a
`{"do": "reload"}` binding — a broken config is rejected and the old one kept.

```json
{
  "devices": ["auto"],
  "base": "qwerty",
  "keymap": { ... },
  "layers": { ... }
}
```

- `devices` — `["auto"]` (every keyboard-looking device, the default) or explicit
  paths like `"/dev/input/by-id/usb-Keychron-event-kbd"`.
- `base` — `"qwerty"` makes every key on a normal keyboard type itself unless
  your keymap says otherwise. Without it, **a key you don't mention does
  nothing**, which is occasionally what you want and usually not.
- `keymap` — the root of the tree.
- `layers` — keymaps with names, so you can use one in more than one place.

## The model

A key gives you two events, press and release. grime has no clocks, so
everything else is derived from those two:

| you mean | grime knows it because |
|---|---|
| the key is **held** | it went down and hasn't come up |
| the key was **tapped** | it came up and nothing else was pressed in between |
| the key was **used as a modifier** | something else *was* pressed in between |

That is the whole vocabulary. There is no "held for 200ms", and there is no
event at the moment a press becomes a hold — a hold is a *state*, not a moment.

## Keys

Key names are evdev names lowercased (`grime --list-keys`): `a`, `1`,
`leftshift`, `capslock`, `f13`, `kpenter`. `shift ctrl alt super` are aliases.

An entry's name can also be:

| name | means |
|---|---|
| `"f"` | the f key |
| `"ctrl+f"` | f while ctrl is held — **both** ctrl keys, since `ctrl` doesn't pick a side |
| `"rightalt x f"` | press rightalt, then x, then f — an Emacs-style sequence |
| `"capslock ctrl+f"` | the two combined; a chord may only be the **last** step |

Entries that share a prefix merge, so `"ctrl+f"` and `"ctrl+t"` build one ctrl
layer between them.

## What a key does

The value is either a **key to behave like**, or an object of **slots**:

```json
"a": "a",                                       // be the a key
"rightshift": "a",                              // be the a key too
"f13": "ctrl+z",                                // be a ctrl+z key, hold and all
"f1": { "do": "exec", "cmd": "firefox" },       // an action, on press
"capslock": {
  "tap":  "esc",                                // tapped alone -> Esc
  "hold": { "h": "left", "j": "down" }          // held -> a layer
},
"a": null                                       // explicitly dead
```

| slot | takes | what it does |
|---|---|---|
| `hold` | a keymap | that keymap applies while this key is held |
| `prefix` | a keymap | Emacs style: applies until one action fires, or an undefined key cancels it |
| `toggle` | a keymap | sticky: applies until this same binding is pressed again |
| `tap` | an action | fires on release, only if nothing else was pressed meanwhile |
| `release` | an action | fires on release, always |
| `press` | an action | fires on press |
| `do` + params | — | the same as `press`, just shorter |
| `pass` | `true` | keys the layer doesn't define fall back to the keymap outside it |

One layer slot per key, and `tap` and `release` are mutually exclusive.
Otherwise they combine freely: `{"tap": …, "hold": …}` is the classic
home-row mod, and `{"toggle": …, "do": …}` announces itself as it flips.

An **action** is `{"do": "...", ...}`, or a string: `"esc"` taps that key,
`"ctrl+z"` taps that chord.

## How the walk works

1. A **press** is looked up in the innermost live layer (and, with `pass`, the
   ones outside it). No match → nothing happens, and an Emacs prefix is cancelled.
2. A **release** is looked up in the keymap where that key's **press** was
   handled, so press and release stay paired even if the layers changed in
   between.
3. Live layers are a **set, not a path**. Each one leaves on its own terms — a
   `hold` layer when its key comes up, a `prefix` when an action fires or an
   undefined key cancels it, a `toggle` when you press it again — and layers
   outside it coming and going don't disturb it.
4. Key repeat events are ignored; the OS autorepeats held output keys itself.
5. There is no notion of time anywhere in the keymap.

`grime --expand` prints the tree these rules actually run on.

## Actions

| `do`      | params | |
|-----------|--------|---|
| `press`   | `key` | press a key (and keep it held) |
| `release` | `key` | release it |
| `tap`     | `key`, `mods` (array, optional) | press+release, with modifiers held around it |
| `type`    | `text` | type a string (US QWERTY, ASCII, `\n`, `\t`) |
| `exec`    | `cmd` (via `/bin/sh -c`) or `argv` (array) | run a command in the background, as you (not root) |
| `http`    | `url`, `method` (GET), `headers` (array), `body` (string or JSON), `timeout_ms` (10000) | async HTTP request; `${VAR}` expands from the environment when it fires |
| `seq`     | `actions` (array) | run several actions in order |
| `log`     | `msg` | print to grime's log |
| `reload`  | | re-read the config file |
| `quit`    | | exit and give the keyboard back |

`grime --list-actions` prints what your build has.

## Recipes

**Tap-or-hold**, the reason capslock exists:
```json
"capslock": { "tap": "esc", "hold": { "h": "left", "j": "down", "k": "up", "l": "right" } }
```

**Emacs prefix** — `rightalt x f`:
```json
"rightalt x f": { "do": "exec", "cmd": "xdg-open ~" }
```

**Override mode** — take over some app shortcuts (Emacs, Firefox, Slack; grime
sits below all of them) and let the rest through. `capslock o` switches it on
and off, and says so both times:
```json
"keymap": {
  "capslock": { "hold": {
    "o": { "toggle": "override", "pass": true,
           "do": "exec", "argv": ["notify-send", "grime", "override mode"] } } }
},
"layers": {
  "override": { "ctrl+f": { "do": "exec", "cmd": "firefox" } }
}
```
- `pass` is what lets `ctrl+s` still reach the app as `ctrl+s`.
- Ctrl is really held while the override runs, so an `exec` or `http` is fine
  but a `tap` becomes another ctrl chord (`ctrl+f` → `ctrl+l`).
- A toggle survives whatever you turned it on from: capslock can come back up
  and the override stays. Turn it off with the same path.
- Two uses of one named layer are two *independent* toggles.

**A second layout** — `configs/default.json` toggles Dvorak with `capslock d`;
the layer is 8 lines of `"physical": "what it should type"` and `pass` handles
every key the two layouts agree on.

**Lights**: `configs/examples/lights.json` (Home Assistant + Hue). Export
`HA_TOKEN` before starting grime.

**Letters as modifiers**: `configs/examples/weird-modifiers.json`. Note the
classic home-row-mod caveat: with `a` as a hold-modifier, rolling `a` into `s`
quickly means "s inside a's layer", not "as".

## Safety
- Emergency exit: hold `Esc` + `Backspace` for 1 s (checked before the keymap, always works).
- `--timeout SEC` auto-exits. `--dry-run` logs without grabbing or emitting.
- grime waits for all keys to be released before grabbing.
