{ lib, stdenvNoCC, fetchurl }:

let
  baseUrl = "https://download.moonshine.ai/model/medium-streaming-en/quantized_26_08_21";
  files = [
    { name = "adapter.ort"; hash = "sha256-Pyoofe9XzAlDZ6Duw8T1/DajLsQg6GdktpaSCZGyAoE="; }
    { name = "cross_kv.ort"; hash = "sha256-ZC9uIc0wW+eTQiB8b55raB1GnVW8SMcrJ7hIRvtx/R4="; }
    { name = "decoder_kv.ort"; hash = "sha256-GTuzZkkrdPxK0zjGd46NjrkWqqEbWqJk+QV/Tbd1lIY="; }
    { name = "encoder.ort"; hash = "sha256-EpFeduusfdKHxepjll0GEDpTuhziQqSjTzGPOVjGDDc="; }
    { name = "frontend.model.ort"; hash = "sha256-lXaIVccMglHu7MBf7faZmdobirFvYFyfRX/TNUsK1rU="; }
    { name = "frontend.weights.ort"; hash = "sha256-WslB9JDL4DWzNbmaQUzDk9YtTG+fJCNJWyhocNJx1wk="; }
    { name = "streaming_config.json"; hash = "sha256-KOg7eijpFHJpKgNeDa4xFkIq5DrrK+9e2CLETOibiK8="; }
    { name = "tokenizer.bin"; hash = "sha256-aISzX9Y3fUxNMjNqC8FS82tk0eRbZQNoPNwjglCoRy0="; }
  ];
  sources = map (file: file // {
    src = fetchurl {
      url = "${baseUrl}/${file.name}";
      inherit (file) hash;
    };
  }) files;
in
stdenvNoCC.mkDerivation {
  pname = "moonshine-medium-streaming-en";
  version = "2026-08-21";
  dontUnpack = true;

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    ${lib.concatMapStringsSep "\n" (file:
      "ln -s ${file.src} $out/${lib.escapeShellArg file.name}") sources}
    total_bytes=$(du --dereference --bytes --total $out/* | tail -n1 | cut -f1)
    if [ "$total_bytes" -gt $((400 * 1024 * 1024)) ]; then
      echo "Moonshine model exceeds 400 MiB: $total_bytes bytes" >&2
      exit 1
    fi
    runHook postInstall
  '';

  meta = {
    description = "Quantized Moonshine Medium Streaming English speech model";
    homepage = "https://moonshine-voice.readthedocs.io/en/latest/models/";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
  };
}
