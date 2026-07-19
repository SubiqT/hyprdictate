{
  hyprland,
  lib,
  nix-gitignore,
  cmake,
  ninja,
  pkg-config,
  lua5_5,
  nlohmann_json,
}:
# Build the plugin with Hyprland's stdenv so the compiler,
# libstdc++, and PIE flags match the compositor. Any mismatch shows
# up as an obscure symbol resolution error at plugin load rather
# than as a build failure, so this must not fall back to the generic
# stdenv.
hyprland.stdenv.mkDerivation {
  pname = "hyprdictate-plugin";
  version = "0.1";

  src = nix-gitignore.gitignoreSource [ ] ./.;

  nativeBuildInputs = [ cmake ninja pkg-config ];

  # hyprland.dev provides pkg-config discovery for the hyprland .pc
  # file. hyprland.buildInputs pulls in aquamarine, hyprutils,
  # hyprlang, etc. that Hyprland's headers transitively include.
  # lua5_5 is required for hl.plugin.hyprdictate.* Lua registration
  # since hyprland.pc does not chain Lua in. nlohmann_json is
  # PUBLIC-linked by hyprdictate::shared, so the plugin needs it on
  # its own include path even though shared/ is built here too.
  buildInputs = [
    hyprland.dev
    lua5_5
    nlohmann_json
  ] ++ hyprland.buildInputs;

  cmakeFlags = [
    (lib.cmakeBool "HYPRDICTATE_BUILD_DAEMON" false)
    (lib.cmakeBool "HYPRDICTATE_BUILD_CLI"    false)
    (lib.cmakeBool "HYPRDICTATE_BUILD_PLUGIN" true)
  ];

  buildPhase = "ninjaBuildPhase";
  enableParallelBuilding = true;

  meta = with lib; {
    homepage = "https://github.com/SubiqT/hyprdictate";
    description = "Hyprland plugin for hyprdictate voice dictation";
    license = licenses.mit;
    platforms = platforms.linux;
  };
}
