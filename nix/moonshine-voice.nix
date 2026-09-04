{ lib, stdenvNoCC, fetchurl, autoPatchelfHook, stdenv }:

let
  artifacts = {
    x86_64-linux = {
      arch = "x86_64";
      hash = "sha256-nDqH/qk/8q2VeTiGj5Wgo2bc6f+K2GvebNz1pMrbUd8=";
    };
    aarch64-linux = {
      arch = "arm64";
      hash = "sha256-FgDICggGt6JYIwfJjnpW9AcuSwYEmLCKC3XrIK9C3vI=";
    };
  };
  artifact = artifacts.${stdenvNoCC.hostPlatform.system}
    or (throw "moonshine-voice: unsupported platform ${stdenvNoCC.hostPlatform.system}");
in
stdenvNoCC.mkDerivation {
  pname = "moonshine-voice";
  version = "0.1.5";

  src = fetchurl {
    url = "https://github.com/moonshine-ai/moonshine/releases/download/v0.1.5/moonshine-voice-linux-${artifact.arch}.tar.gz";
    inherit (artifact) hash;
  };

  nativeBuildInputs = [ autoPatchelfHook ];
  buildInputs = [ stdenv.cc.cc.lib ];

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp -r include lib $out/
    runHook postInstall
  '';

  meta = {
    description = "On-device streaming speech recognition runtime";
    homepage = "https://github.com/moonshine-ai/moonshine";
    license = lib.licenses.mit;
    platforms = builtins.attrNames artifacts;
  };
}
