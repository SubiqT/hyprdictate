{
  description = "hyprdictate — voice dictation daemon and Hyprland plugin";

  # Hyprland is pinned to v0.55.4 to match hyprwsmode, so a nix-config
  # that consumes both plugins can share one hyprland input via
  # `inputs.hyprdictate.inputs.hyprland.follows = "hyprland"` without
  # a version divergence. `submodules=1` is required because Hyprland
  # vendors hyprland-protocols, udis86, and tracy as submodules.
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    hyprland.url = "git+https://github.com/hyprwm/Hyprland?submodules=1&ref=v0.55.4";
  };

  outputs = { self, nixpkgs, hyprland, ... }:
    let
      # Iterate the same systems Hyprland itself builds for so the
      # plugin output's system attribute set matches hyprland's
      # exactly. Today that is x86_64-linux and aarch64-linux via
      # hyprland.inputs.systems.
      forAllSystems = fn:
        nixpkgs.lib.genAttrs
          (builtins.attrNames hyprland.packages)
          (system: fn system nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (system: pkgs: rec {
        # Daemon binary + systemd unit. Runtime deps: whisper-cpp,
        # pipewire, wtype (via makeWrapper on PATH).
        daemon = pkgs.callPackage ./default.nix {
          component = "daemon";
        };

        # CLI client. Runtime deps: none beyond the standard C++
        # runtime — the CLI links only nlohmann_json + CLI11 + POSIX
        # sockets.
        cli = pkgs.callPackage ./default.nix {
          component = "cli";
        };

        # Compositor plugin. Built with Hyprland's stdenv against the
        # pinned Hyprland source so ABI matches.
        plugin = pkgs.callPackage ./plugin.nix {
          hyprland = hyprland.packages.${system}.hyprland;
        };

        # symlinkJoin so a consumer that wants everything in one
        # closure can reference `hyprdictate.packages.${system}.default`.
        # Component derivations remain individually addressable.
        default = pkgs.symlinkJoin {
          name    = "hyprdictate";
          paths   = [ daemon cli plugin ];
        };
      });

      # Dev shell with every dep on the search path so `nix develop`
      # gives a working build environment with clangd support. Uses
      # hyprland.stdenv so the shell can compile the plugin too.
      devShells = forAllSystems (system: pkgs: {
        default = pkgs.mkShell.override {
          stdenv = hyprland.packages.${system}.hyprland.stdenv;
        } {
          name = "hyprdictate";
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
            pkgs.clang-tools
          ];
          buildInputs = [
            pkgs.asio
            pkgs.cli11
            pkgs.nlohmann_json
            pkgs.spdlog
            pkgs.tomlplusplus
            pkgs.whisper-cpp
            pkgs.pipewire
            pkgs.wtype
            pkgs.lua5_5
            hyprland.packages.${system}.hyprland.dev
          ] ++ hyprland.packages.${system}.hyprland.buildInputs;
        };
      });
    };
}
