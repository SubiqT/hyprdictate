{
  description = "hyprdictate — voice dictation daemon for Hyprland";

  # M1 does not depend on Hyprland: the daemon and the CLI build
  # against plain nixpkgs. The plugin half arrives in M2 with a
  # separate hyprland input and its own package output.
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs, ... }:
    let
      # Match the systems Hyprland itself builds for so the M2 plugin
      # output slots in without a system-list divergence.
      supportedSystems = [ "x86_64-linux" "aarch64-linux" ];

      forAllSystems = fn:
        nixpkgs.lib.genAttrs supportedSystems
          (system: fn system nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (system: pkgs: rec {
        # Single derivation builds both `hyprdictated` (daemon) and
        # `hyprdictate` (CLI). Consumers pick either by referencing
        # ${hyprdictate}/bin/hyprdictated or /bin/hyprdictate.
        hyprdictate = pkgs.callPackage ./default.nix { };

        default = hyprdictate;
      });

      # Convenience dev shell so `nix develop` gives a working build
      # environment with all deps and clangd support.
      devShells = forAllSystems (system: pkgs: {
        default = pkgs.mkShell {
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
          ];
        };
      });
    };
}
