# grime

C11 daemon: grabs keyboards (evdev), walks press/release events through a JSON keymap tree, fires actions (uinput key output, exec, http) on a single epoll loop.

- Build `make`; test `make test` (no root); validate configs `make check-configs`.
- Contract lives in `include/grime/*.h` — keep header changes in separate PRs (see docs/CONTRIBUTING.md).
- `src/engine` and `src/config` must not include Linux headers.
- Nothing on the loop may block. New actions: one file in `src/actions/`, `GRIME_REGISTER_ACTION`.
- Kernel-style C: tabs, `.clang-format`. Log via `LOG_*` from `grime/log.h`.
- Never run grime for real (grabbing the keyboard) without the user's OK; `--dry-run` is safe.
