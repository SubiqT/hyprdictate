# hyprdictate

Voice dictation for Hyprland with on-device Moonshine streaming speech
recognition. Press a keybind, see a live replaceable transcript in Noctalia,
and press again to inject the finalized text into the window that was focused
when recording began.

## Behaviour

- PipeWire supplies 16 kHz mono PCM directly to Moonshine while recording.
- Partial transcripts are broadcast roughly every 300 ms and forwarded by the
  Hyprland plugin to socket2 for live UI preview.
- Partial text is **never typed into applications**. Moonshine may revise it.
- Stopping drains the stream, emits one final transcript, and injects that final
  text into the original target window only after all physical keys, pointer
  buttons, and modifiers are released. If input remains held for two seconds, injection is
  dropped rather than interpreted as compositor shortcuts.
- Cancelling while recording discards the stream immediately. Cancelling while
  the final drain is in progress marks the session cancelled and drops its
  result when Moonshine returns.

The repository contains the daemon (`hyprdictated`), CLI (`hyprdictate`), and
Hyprland plugin. The sibling
[`noctalia-hyprdictate`](https://github.com/SubiqT/noctalia-hyprdictate)
repository renders the live preview.

## Requirements

- Linux with PipeWire and Hyprland 0.55+
- Moonshine Voice 0.1.5 (the Nix flake packages the pinned Linux runtime)
- A Moonshine English streaming model directory
- `wtype` for recordings started through the standalone CLI

## Nix build

```console
nix build .#daemon
nix build .#cli
nix build .#plugin
nix build .#moonshine-model-medium-en
```

The model output is the quantized English Medium Streaming model (245M
parameters, about 257 MiB). It is pinned as eight fixed-output downloads and is
under Moonshine's MIT model license.

Consumers can use the packaged model directly in their generated TOML:

```nix
model = inputs.hyprdictate.packages.${pkgs.system}.moonshine-model-medium-en;

xdg.configFile."hyprdictate/config.toml".text = ''
  model_path = "${model}"
  model_arch = "medium-streaming"
  language = "en"

  [moonshine]
  update_interval_ms = 300
'';
```

The daemon and model are separate outputs so systems that provide their own
model directory do not acquire the model closure automatically.

## Manual build

CMake expects Moonshine's `moonshine-c-api.h` and `libmoonshine.so`, plus
PipeWire, CLI11, toml++, spdlog, Asio, and nlohmann/json:

```console
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Configuration

```toml
model_path = "/path/to/medium-streaming-en"
model_arch = "medium-streaming" # tiny-streaming | small-streaming | medium-streaming
language = "en"
inject_focus = "start"
inject_method = "wtype"

[moonshine]
# Moonshine internally coalesces updates below 200 ms. Lower values are rejected.
update_interval_ms = 300

[vocabulary]
# Applied through Moonshine's streaming keyterm biasing API.
global = ["Hyprland", "NixOS", "Noctalia"]
include_title_tokens = false

[indicator]
border = false
border_color = "0xffff5555"
```

Only English streaming models are currently accepted. `model_path` must point
to a directory containing the canonical Moonshine ORT assets and
`tokenizer.bin`, not a Whisper GGML file.

## Running

```console
hyprdictated --config ~/.config/hyprdictate/config.toml
hyprdictate toggle
hyprdictate cancel
hyprdictate status
```

Recommended Hyprland bindings through the compositor plugin:

```lua
hl.bind("SUPER + H",         function() return hl.plugin.hyprdictate.toggle() end)
hl.bind("SUPER + SHIFT + H", function() return hl.plugin.hyprdictate.cancel() end)
```

Plugin-started recordings capture the focused window and use deterministic
plugin-side injection. CLI-started recordings have no captured compositor
window and fall back to daemon-side `wtype`.

## Wire protocol

Transcript events are line-delimited JSON:

```json
{"event":"transcript","text":"replaceable preview","final":false}
{"event":"transcript","text":"final text","final":true}
```

Clients parsing older events that omit `final` treat them as final for rolling
upgrade compatibility. The plugin forwards text onto Hyprland socket2 as a
JSON string so punctuation and newlines cannot corrupt its line protocol.

## Licence

MIT. See [LICENSE](LICENSE). Moonshine's English models and runtime are also
MIT-licensed.
