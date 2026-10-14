# Config

One JSON file. Default location `~/.config/grime/config.json`, or `grime -c PATH`.
Validate with `grime --check -c PATH`. See what it really means with
`grime --expand -c PATH`. Reload a running grime with `kill -HUP` or a
`{"do": "reload"}` binding — a broken config is rejected and the old one kept.

```json
{
  "devices": ["auto"],
  "include": ["layouts/qwerty.json"],
  "base": "qwerty",
  "emergency": ["esc", "backspace"],
  "keymap": { ... },
  "layers": { ... }
}
```

- `devices` — which input devices to take, and on what terms. See below.
- `include` — other JSON files to pull `layers` and `keymap` entries from.
- `base` — the name of a layer to fill in with: every key in it types itself
  unless your keymap says otherwise. Without a base, **a key you don't mention
  does nothing**, which is occasionally what you want and usually not.
- `emergency` — the keys that, held together for a second, give the keyboard
  back. Defaults to `["esc", "backspace"]`; at least two, at most four. See Safety.
- `keymap` — the root of the tree.
- `layers` — keymaps with names, so you can use one in more than one place.

## Devices

`devices` is a list of **matchers**. A device is used if **any** entry matches
it; within one entry **every** field you set must match; and the **first**
entry that matches supplies that device's options.

```json
"devices": [
  "auto",
  "/dev/input/by-path/platform-i8042-serio-0-event-kbd",
  { "kind": "pointer", "name": "TPPS/2*" },
  { "name": "ThinkPad Extra Buttons", "vendor": "17aa", "product": "5054" },
  { "path": "/dev/input/by-id/*Keychron*", "grab": false }
]
```

| field | takes | matched against |
|---|---|---|
| `path` | a glob | the `/dev/input/eventN` node **and** every by-id/by-path symlink pointing at it |
| `name` | a glob | what the device calls itself |
| `phys`, `uniq` | a glob | its physical location / serial |
| `vendor`, `product`, `bus` | a **hex string**, `"17aa"` | its USB/PS2 ids |
| `kind` | `keyboard`, `keys`, `pointer`, `any` | what it can do (below) |
| `grab` | `true` (default) / `false` | exclusive, or observe-only |
| `unmatched` | `"drop"` / `"pass"` | what happens to a key with no binding |

`"auto"` means `{"kind": "keyboard"}` and a bare path means `{"path": "..."}` —
the two spellings that came first still mean exactly what they did.

**`kind`** is what a device can do, not what it's called:
- `keyboard` — has a full alphabet. The thing you type on.
- `keys` — has real keys but no alphabet: a laptop's fn row, a hotkey block, a
  macropad. **These are invisible to `"auto"`**, which is why your mic-mute and
  brightness keys need naming before grime can see them.
- `pointer` — buttons and relative motion: a mouse, a trackpoint.
- `any` — no capability filter. Spell it out; an empty matcher is an error.

Ids are hex strings and only hex strings, because `5054` read as decimal is a
different device you'd find weeks later.

```sh
grime --list-devices     # every device, its kind, and which entry would take it
grime --watch            # what your keys actually send, without grabbing anything
```

Run that before editing the list. It calls the same matcher the daemon does, so
it cannot give you a second opinion.

### Grabbed, or just watched

- A device grime **grabs** is exclusive: the OS sees only what grime emits, and
  a key with no binding does nothing (`"unmatched": "pass"` re-emits it
  unchanged instead — the default for pointers, so an unbound button is not a
  dead mouse).
- A device with `"grab": false` is **observe-only**: bindings fire *in addition*
  to what the key normally does. You can add behaviour there, never replace it.
- grime forwards a grabbed pointer's motion and wheel through a virtual pointer
  of its own, so the cursor keeps working. It does **not** forward absolute
  axes, so it will not grab a touchpad or a touchscreen.
- Forwarding a trackpoint this way loses `INPUT_PROP_POINTING_STICK`, and with
  it whatever acceleration profile libinput picks for pointing sticks.
- Grabbing a device also hides its switches (lid, rfkill). grime says so when it
  does; `--list-devices` warns you beforehand.
- Caps, num and scroll lock lights follow. The OS sets those by lighting up
  whatever keyboard it thinks is typing, which once grime has grabbed yours is
  grime's virtual one, so grime pushes the state back onto the real keyboard.

### Plugging things in

grime watches `/dev/input`, so a device you plug in later is matched, opened and
grabbed on its own — it waits for *that* device's keys to be up, not everyone's.
Unplug one and grime drops it and keeps going, waiting for it to come back.

This needs read access to the new node **as the user grime is running as**. Run
`scripts/setup-permissions.sh` once (it adds you to the `input` group) and it
works. Under `sudo` it does not: grime opens the devices as root and then drops
to you, so anything appearing afterwards is unreadable. grime says which node
and why rather than going quiet.

grime knows nothing about keyboard layouts. QWERTY is a layer in
`configs/layouts/qwerty.json`, Dvorak is a layer in `configs/layouts/dvorak.json`,
and both are plain config you can read and edit.

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

**Mouse buttons are keys too**, and keep their evdev prefix: `btn_left`,
`btn_right`, `btn_middle`, `btn_side`, `btn_extra`, `btn_forward`, `btn_back`,
`btn_task`, with the friendlier `leftclick`, `rightclick`, `middleclick`,
`click`. They work on both sides — as the thing you press and as the thing an
action does:

```json
"btn_middle": { "do": "exec", "cmd": "xdg-open ~" },
"capslock":   { "hold": { "btn_left": "btn_right" } },
"f13":        "middleclick"
```

The prefix is not decoration: unprefixed, 23 button names would shadow a key —
`BTN_LEFT` vs the left arrow, `BTN_BACK` vs `KEY_BACK`, `BTN_A` vs `a`. And
there is deliberately **no** `mouse1`/`mouse2`/`mouse3`: X11 numbers buttons
1=left 2=middle 3=right, evdev orders them left, right, middle, so no numbering
reads correctly to everyone. Spell out the button you mean.

An entry's name can also be:

| name | means |
|---|---|
| `"f"` | the f key |
| `"ctrl+f"` | f while ctrl is held — **both** ctrl keys, since `ctrl` doesn't pick a side |
| `"rightalt x f"` | press rightalt, then x, then f — an Emacs-style sequence |
| `"capslock ctrl+f"` | the two combined; a chord may only be the **last** step |

Entries that share a prefix merge, so `"ctrl+f"` and `"ctrl+t"` build one ctrl
layer between them.

**A chord uses its modifiers up.** `"ctrl+a": "home"` sends a bare Home, not
Ctrl+Home: grime lets go of Ctrl at the output just before the binding's first
key, and presses it again before the next key that doesn't use it up, if you are
still holding it. Hold Ctrl and press `a`, `e`, `s`, and the app sees Home, End
and Ctrl+S. A modifier the chord doesn't name is left alone, so Shift held
through `ctrl+a` still selects. Binding a chord to the same chord
(`"ctrl+w": "ctrl+x"`) works as you would expect.

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
| `mode` | a keymap | applies while you use it: a key it doesn't define ends it, then does what it would have done without it |
| `tap` | an action | fires on release, only if nothing else was pressed meanwhile |
| `release` | an action | fires on release, always |
| `press` | an action | fires on press |
| `do` + params | — | the same as `press`, just shorter |
| `pass` | `true` | keys the layer doesn't define fall back to the keymap outside it |
| `leave` | `true` | after this binding fires, the layer it is in ends; with `mode`, that mode starts in its place |
| `on` | `true` | with `toggle`: the layer starts switched on |

One layer slot per key, and `tap` and `release` are mutually exclusive.
`leave` can stand alone (`"ctrl+g": {"leave": true}`) or ride along with an action,
and the only layer slot it takes is `mode`.
Otherwise they combine freely: `{"tap": …, "hold": …}` is the classic
home-row mod, and `{"toggle": …, "do": …}` announces itself as it flips.

An **action** is `{"do": "...", ...}`, or a string: `"esc"` taps that key,
`"ctrl+z"` taps that chord.

## How the walk works

1. A **press** is looked up in the innermost live layer (and, with `pass`, the
   ones outside it). No match → nothing happens, and an Emacs prefix is cancelled.
   In each layer, the modifiers you are holding are followed first: with Ctrl
   down, `a` is looked up under that layer's `ctrl`, then in the layer itself.
   So `pass` holds for chords too — a layer that defines `ctrl+f` still lets
   `ctrl+t` fall through to whatever `ctrl+t` means outside it — and a layer
   that comes or goes while Ctrl is held sees that Ctrl.
2. A **release** is looked up in the keymap where that key's **press** was
   handled, so press and release stay paired even if the layers changed in
   between.
3. Live layers are a **set, not a path**. Each one leaves on its own terms — a
   `hold` layer when its key comes up, a `prefix` when an action fires or an
   undefined key cancels it, a `toggle` when you press it again, a `mode` when
   you press a key it doesn't define (a modifier on its own doesn't count) or
   it again — and layers outside it coming and going don't disturb it. A
   `leave` binding ends the layer it was found in.
4. Key repeat events are ignored; the OS autorepeats held output keys itself.
5. There is no notion of time anywhere in the keymap.

`grime --expand` prints the tree these rules actually run on. A named layer
stays one layer however many keys use it, and may lead back to itself: there
it is `"then": "<name>"`, spelled out once under `"layers"`.

## Several files

`include` takes paths, resolved against the directory of the file that names
them (absolute and `~/` work too). Each included file may have `include`,
`layers` and `keymap`; only the main config sets `devices`, `emergency` and `base`.

```json
"include": ["layouts/qwerty.json", "layouts/dvorak.json", "~/.config/grime/work.json"]
```

**The first definition wins**, and your own file counts first — so a `keymap`
entry or a `layer` you write yourself overrides one you pulled in, and among
includes the earlier one wins. Including the same file twice is harmless.

This is how layouts work: `configs/layouts/qwerty.json` is nothing but

```json
{ "layers": { "qwerty": { "a": "a", "b": "b", ... } } }
```

so `"base": "qwerty"` is just "seed the root with that layer". Write your own
layout the same way, point `base` at it, and grime never has to know it exists.
`scripts/gen-qwerty.py` regenerates the QWERTY one.

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
| `kat`     | `mode` (`left_click`, `grab_release`, `right_click`), `settings` (path to kat's settings file), `trigger` | open kat's pick-by-code grid; the key bound to it is the grid's trigger. Built when libkat is installed |

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
- Ctrl is used up by the chord, so `"ctrl+f": "f1"` sends a bare F1; write
  `"ctrl+f": "ctrl+l"` if you want another ctrl chord.
- A toggle survives whatever you turned it on from: capslock can come back up
  and the override stays. Turn it off with the same path.
- Two uses of one named layer are two *independent* toggles.

**Emacs keys in every text field**: `configs/examples/emacs.json` includes
`configs/layers/emacs.json` and turns it on at start with `ctrl+capslock` to
switch it off and on again:
```json
"ctrl+capslock": { "toggle": "emacs", "pass": true, "on": true }
```
The toggle isn't inside the layer, but `pass` reaches it: with Ctrl held,
`capslock` falls through the emacs layer's ctrl to the root's.

| keys | send |
|---|---|
| C-f C-b C-n C-p, C-a C-e, C-v M-v | arrows, Home/End, Page Down/Up |
| M-f M-b, M-< M->, M-m | word right/left, top/bottom, Home |
| M-a M-e, M-{ M-} | Ctrl+Up/Down (the nearest thing to sentence and paragraph) |
| C-S-f C-S-b C-S-n C-S-p C-S-a C-S-e, M-S-f M-S-b | the same motions, selecting |
| C-d C-h, M-d M-DEL | Delete, Backspace, and by word |
| C-k, C-o, C-t | cut to end of line, open line, transpose characters |
| C-w M-w C-y | cut, copy, paste |
| C-/ C-_, C-? | undo, redo (Ctrl+Shift+Z) |
| C-j C-m, C-i, C-[, M-; | Enter, Tab, Esc, Ctrl+/ (comment) |
| C-s C-r | find; again for next/previous (F3, Shift+F3); RET, Esc or C-g closes it |
| M-%, M-g g | replace (Ctrl+H), go to line (Ctrl+G) |
| C-x h, C-x u, C-x k | select all, undo, close tab |
| C-x C-s, C-x C-w, C-x C-f, C-x C-c | save, save as, open, quit |
| C-x ←/→ | previous/next tab |
| C-g | Esc |

- **C-SPC** starts a `mode` where motion selects: `ctrl+f` becomes Shift+Right,
  `alt+f` Ctrl+Shift+Right, and so on. `C-w` cuts and `M-w` copies the
  selection and end the mode. `C-g` drops the selection, at the end the first
  motion went to (a bare Right or Left), and C-SPC does the same and starts a
  new one there. Arrows, Home/End and Page Up/Down extend it, as in Emacs; any
  other key ends it and goes through, so typing replaces the selection. After
  `C-x h`, `C-g` leaves the cursor at the top.
- The first motion picks `markf` or `markb` with `{"leave": true, "mode": …}`,
  one mode for another; C-SPC in those goes back to `mark`.
- grime can't see which window has focus, so the layer is everywhere. Turn it
  off for a terminal or Emacs with `ctrl+capslock`.
- C-k is Shift+End, then cut: at the end of a line there is nothing to select,
  so it does not join lines the way Emacs does. C-t goes through the clipboard.
- The search layer stays on until RET, Esc or C-g; click away instead and the
  next C-s is F3, find next.
- Not here, for want of a key every app agrees on: M-t, M-u/M-l/M-c, M-y, M-z,
  M-q, C-l, C-u, M-x, C-x C-x, C-S-DEL, rectangles and keyboard macros.
- The app shortcuts these keys replace (select all, find, new window, print,
  close tab, the command palette on C-S-p, menu accelerators on Alt) are gone
  while the layer is on.

**kat**, the pick-by-code grid (needs root; see Safety for `--user`):
```json
"capslock": { "do": "kat", "settings": "/etc/kat/config.json" }
```
Capslock opens the grid and closes it again. While the grid is up, or a drag
waits for its drop, every key goes to kat; grime takes the keyboard back when
the pick ends. Hold Left Alt or Right Alt as you press it for a drag or a
right click, as kat's settings say.

**A second layout** — `configs/default.json` toggles Dvorak with `capslock d`;
the layer is 8 lines of `"physical": "what it should type"` and `pass` handles
every key the two layouts agree on.

**A ThinkPad**: `configs/examples/thinkpad.json`. Three devices, three
different arrangements:
```json
"devices": [
  "auto",
  { "kind": "pointer", "name": "TPPS/2*" },
  { "name": "ThinkPad Extra Buttons", "vendor": "17aa", "product": "5054", "grab": false }
]
```
- the built-in keyboard, as always;
- the TrackPoint, **grabbed**, so `btn_middle` can become something else. Its
  motion is forwarded, and left and right still click because pointers pass
  unmatched buttons through by default;
- the fn-key block, **observed**. `kind` is `keys` there, not `keyboard`, so
  `"auto"` never saw it. Not grabbed, because those keys already do useful
  things and grabbing would also hide the rfkill switch — bindings on it add
  behaviour rather than replacing it.

Because every device feeds one keymap, holding `capslock` on the keyboard and
clicking on the TrackPoint is a single chord.

**Lights**: `configs/examples/lights.json` (Home Assistant + Hue). Export
`HA_TOKEN` before starting grime.

**Letters as modifiers**: `configs/examples/weird-modifiers.json`. Note the
classic home-row-mod caveat: with `a` as a hold-modifier, rolling `a` into `s`
quickly means "s inside a's layer", not "as".

## Safety
- Emergency exit: hold `Esc` + `Backspace` for 1 s, or all three mouse buttons
  (`btn_left` + `btn_right` + `btn_middle`). Both are checked before the keymap,
  so no binding can shadow them. The mouse one exists because a setup where
  grime owns the pointer still has to be escapable with the pointer.
- `"emergency": ["f12", "f11"]` changes the keyboard chord (two to four keys).
  `"emergency": false` turns **both** off — then `--timeout`, or killing grime
  from another machine, is all you have. grime says so loudly at startup.
- `--timeout SEC` auto-exits. `--dry-run` logs without grabbing or emitting.
- grime waits for all keys to be released before grabbing.
- `--user NAME` keeps grime root after it opens the devices — kat needs that
  to draw over the desktop — and runs `exec` commands as NAME, with NAME's
  session environment (`XDG_RUNTIME_DIR`, the session bus, `WAYLAND_DISPLAY`,
  `DISPLAY`; whatever grime itself was started with wins). Without it, grime
  started under `sudo` drops to you once the devices are open.
- `grime --list-devices` and `grime --watch` never grab and never emit. Reach
  for `--watch` when you want to know what a key sends before binding it.
- grime refuses to grab a device with absolute axes (touchpads, touchscreens),
  because it cannot forward those and a grabbed one would simply stop working.
