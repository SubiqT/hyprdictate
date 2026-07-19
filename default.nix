{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  makeWrapper,
  nix-gitignore,
  # C++ libraries
  asio,
  cli11,
  nlohmann_json,
  spdlog,
  tomlplusplus,
  whisper-cpp,
  # System libraries
  pipewire,
  # Runtime dependencies wrapped onto PATH
  wtype,
}:
# Standard nixpkgs stdenv is fine here: hyprdictated is a plain
# systemd user service, not a Hyprland plugin. The plugin half of
# hyprdictate (added in M2) will need hyprland.stdenv to match
# libstdc++ ABI with the compositor, but the daemon and CLI compile
# and run against the default toolchain the rest of nixpkgs uses.
stdenv.mkDerivation {
  pname = "hyprdictate";
  version = "0.1";

  # nix-gitignore mirrors the repo's .gitignore into the sandboxed
  # source snapshot so build/, compile_commands.json, and editor
  # droppings don't wander into the store hash.
  src = nix-gitignore.gitignoreSource [ ] ./.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    makeWrapper
  ];

  # buildInputs cover every dep the top-level CMakeLists.txt's
  # find_package / pkg_check_modules calls look for. Anything missing
  # here shows up as a configure-time error rather than a build-time
  # link failure.
  buildInputs = [
    asio
    cli11
    nlohmann_json
    spdlog
    tomlplusplus
    whisper-cpp
    pipewire
  ];

  # The plugin half of the repo needs Hyprland's build environment;
  # the M1 daemon does not. Toggling this option off keeps the plugin
  # subdirectory out of the daemon's Nix build entirely.
  cmakeFlags = [
    "-DHYPRDICTATE_BUILD_PLUGIN=OFF"
  ];

  # Match hyprwsmode's use of ninja for faster incremental builds.
  buildPhase = "ninjaBuildPhase";
  enableParallelBuilding = true;

  # wtype is a runtime dependency (the daemon spawns it via
  # execlp("wtype", ...)). Prepending its bin dir onto PATH via
  # makeWrapper is the standard nixpkgs pattern for subprocess deps
  # that don't need to be linked in. Systemd units invoke the daemon
  # via its wrapped path, so this stays effective under `systemd
  # --user`.
  postFixup = ''
    wrapProgram $out/bin/hyprdictated \
      --prefix PATH : ${lib.makeBinPath [ wtype ]}
  '';

  meta = with lib; {
    homepage = "https://github.com/SubiqT/hyprdictate";
    description = "Voice dictation daemon for Hyprland (M1: standalone daemon + CLI)";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "hyprdictate";
  };
}
