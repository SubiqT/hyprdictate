{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  makeWrapper,
  nix-gitignore,
  # C++ libraries shared by all components
  asio,
  cli11,
  nlohmann_json,
  # daemon-only libraries
  spdlog ? null,
  tomlplusplus ? null,
  whisper-cpp ? null,
  pipewire ? null,
  wtype ? null,
  # Which components to build. Each nix output flips exactly one of
  # these ON so its runtime closure covers only what that binary
  # needs. Component defaults here (all off) force each callsite in
  # flake.nix to declare its intent explicitly rather than inherit a
  # surprise.
  component ? "daemon", # "daemon" | "cli"
}:
# Standard nixpkgs stdenv is fine for the daemon and CLI: neither
# needs to ABI-match Hyprland. The plugin (built via plugin.nix)
# uses hyprland.stdenv instead.
let
  isDaemon = component == "daemon";
  isCli    = component == "cli";
  pname    = if isDaemon then "hyprdictated" else "hyprdictate";
in
stdenv.mkDerivation {
  inherit pname;
  version = "0.1";

  # nix-gitignore mirrors the repo's .gitignore into the sandboxed
  # source snapshot so build/, compile_commands.json, and editor
  # droppings don't wander into the store hash.
  src = nix-gitignore.gitignoreSource [ ] ./.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
  ] ++ lib.optionals isDaemon [ makeWrapper ];

  # buildInputs cover every dep the top-level CMakeLists.txt's
  # find_package / pkg_check_modules calls look for for the selected
  # component. Anything missing here shows up as a configure-time
  # error rather than a build-time link failure.
  buildInputs = [
    asio
    cli11
    nlohmann_json
  ] ++ lib.optionals isDaemon [
    spdlog
    tomlplusplus
    whisper-cpp
    pipewire
  ];

  # Toggle per-component build gates. The other two components are
  # excluded so their configure-time dep checks don't fire.
  cmakeFlags = [
    (lib.cmakeBool "HYPRDICTATE_BUILD_DAEMON" isDaemon)
    (lib.cmakeBool "HYPRDICTATE_BUILD_CLI"    isCli)
    (lib.cmakeBool "HYPRDICTATE_BUILD_PLUGIN" false)
  ];

  # Match hyprwsmode's use of ninja for faster incremental builds.
  buildPhase = "ninjaBuildPhase";
  enableParallelBuilding = true;

  # wtype is a runtime dependency of the daemon: it's spawned via
  # execlp("wtype", ...). Under Nix the search-PATH inherited by
  # systemd --user doesn't include wtype's bin dir, so prepend it
  # via makeWrapper. The plugin (M2+) injects via wlr_virtual_
  # keyboard_v1 and doesn't need this wrap.
  postFixup = lib.optionalString isDaemon ''
    wrapProgram $out/bin/hyprdictated \
      --prefix PATH : ${lib.makeBinPath [ wtype ]}
  '';

  meta = with lib; {
    homepage = "https://github.com/SubiqT/hyprdictate";
    description =
      if isDaemon
      then "hyprdictate voice dictation daemon"
      else "hyprdictate CLI client";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = pname;
  };
}
