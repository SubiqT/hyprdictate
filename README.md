# hyprdictate

Voice dictation daemon for Hyprland. Toggle a keybind, speak, get the
transcript typed into the window you were focused on when you started.

M1 ships the standalone daemon (`hyprdictated`), a thin CLI
(`hyprdictate`), and a systemd user unit. `hyprdictate toggle` records
audio from PipeWire, runs whisper.cpp inference on the resulting PCM,
and injects the transcript into the focused window via `wtype`.

Hyprland plugin integration (border indicator, deterministic
window-targeted injection via `wlr_virtual_keyboard_v1`) and the
Noctalia widget arrive in later milestones and live in the same
repository.

## Requirements

- Linux with PipeWire and a Wayland compositor
- `whisper.cpp` (via `whisper-cpp` on NixOS / your distro's package)
- `wtype` for M1 text injection
- A GGML whisper model on disk (`ggml-base.en.bin` recommended for
  latency; larger models trade throughput for accuracy)

## Build

### Nix flake

```console
$ nix build .#hyprdictate
$ ./result/bin/hyprdictated --version
```

Consumers wire the flake into their nix-config as:

```nix
{
  inputs.hyprdictate.url = "github:SubiqT/hyprdictate";
}
```

### Manual build

Requires `cmake`, `pkg-config`, and the C++ dependencies on your
system's include/link paths. The build resolves each dep via
`find_package`/`pkg_check_modules` first and falls back to
`FetchContent` for the header-only libraries.

```console
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build --parallel
$ ./build/daemon/hyprdictated --version
$ ./build/cli/hyprdictate --version
```

## Configuration

The daemon reads `$XDG_CONFIG_HOME/hyprdictate/config.toml` (falling
back to `~/.config/hyprdictate/config.toml`). Minimal M1 config:

```toml
model_path = "~/.cache/hyprdictate/ggml-base.en.bin"
language   = "en"
threads    = 4

[vocabulary]
global = ["Hyprland", "NixOS", "Wayland", "Noctalia"]

[whisper]
temperature      = 0.0
no_speech_thold  = 0.6
suppress_blank   = true
```

Every schema key is documented in the code comments under
`daemon/src/config.hpp`. Fields the current milestone does not yet
consume (`indicator`, `inject_focus = "end"`, `vocabulary.per_class`)
are parsed and stored, so a forward-looking config does not trip the
M1 daemon.

## Running

Under systemd (installed by the Nix module, or via the unit file in
`contrib/systemd/`):

```console
$ systemctl --user enable --now hyprdictate.service
$ journalctl --user -u hyprdictate.service | tail
```

Manually, for iterating on the config:

```console
$ hyprdictated --config ~/.config/hyprdictate/config.toml
```

Then bind a keybind in your Hyprland config:

```
bind = SUPER, H, exec, hyprdictate toggle
bind = SUPER SHIFT, H, exec, hyprdictate cancel
```

## CLI

```console
$ hyprdictate toggle          # start / stop dictation
$ hyprdictate cancel          # discard an in-flight recording
$ hyprdictate status          # print daemon state as JSON
$ hyprdictate reload          # (M4) reload config without restart
```

## Roadmap

- **M1 — this milestone.** Standalone daemon, wtype injection,
  vocabulary layer 1 (global).
- **M2.** Hyprland plugin: dispatchers, plugin-side injection via
  `wlr_virtual_keyboard_v1`, per-window border indicator,
  Socket2 event emission, per-class vocabulary.
- **M3.** Noctalia widget in a sibling repo
  (`SubiqT/noctalia-hyprdictate`).
- **M4.** PTT (push-to-talk), `inject_focus = "end"`, title-token
  vocabulary, config reload, model auto-download on first run.

## Licence

MIT — see LICENSE.
