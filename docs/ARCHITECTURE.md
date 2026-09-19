# Architecture

```
 physical keyboard(s)
        │ /dev/input/eventN (EVIOCGRAB)
        ▼
 src/backend/linux/evdev_input.c ──► grime_key_event {code, press|release|repeat}
        │                                      │
        │                         src/core/main.c  (emergency chord, then…)
        │                                      ▼
        │                         src/engine/engine.c  — walks the keymap tree
        │                                      │ binding fires
        │                                      ▼
        │                         src/actions/*  (registry.c looks up "do")
        │                          ├─ press/release/tap/type ─► grime_emit ─► src/backend/linux/uinput_output.c ─► OS
        │                          ├─ exec ─► posix_spawn, child reaped via the loop's signalfd
        │                          └─ http ─► libcurl multi, its sockets + timer live on the loop
        ▼
 src/core/loop.c — one epoll loop: device fds, signalfd (INT/TERM/HUP/CHLD), timerfds, curl sockets
```

**Why single-threaded epoll:** no locks, no races between key events and
actions, and "async" falls out naturally: nothing is allowed to block, slow
things (processes, HTTP) are fds/timers on the loop.

**Contract** (`include/grime/`): the only thing the lanes share.

| header | what | implemented in |
|---|---|---|
| `event.h`    | `grime_key_event` (evdev key codes on every OS) | — |
| `loop.h`     | fds, timers, child watching, SIGHUP | `src/core/loop.c` |
| `input.h`    | open/grab keyboards → events | `src/backend/linux/evdev_input.c` |
| `output.h`   | emit key events | `src/backend/linux/uinput_output.c` |
| `runtime.h`  | what actions can touch; `grime_emit` tracks held keys | `src/core/runtime.c` |
| `action.h`   | action types + registry, `GRIME_REGISTER_ACTION` | `src/actions/registry.c` |
| `engine.h`   | keymap tree + walker | `src/engine/engine.c` |
| `config.h`   | JSON → tree | `src/config/config.c` |
| `keynames.h` | `"capslock"` ↔ 58 | `src/engine/keynames.c` (+ generated `keynames_table.inc`) |

**Engine purity:** `src/engine` and `src/config` never include Linux headers,
so they're unit-tested with a recording output (`tests/test_engine.c`) and will
carry over to macOS/Windows backends unchanged (new dir under `src/backend/`).

**Privileges:** reading `/dev/input` needs root or the `input` group. Under
sudo, grime opens the devices then drops to `$SUDO_UID` so `exec` actions run
as you.

**Reload:** `SIGHUP` or the `reload` action parse the file into a new tree
(on a 0 ms timer, never mid-walk), release any held output keys, and swap.
