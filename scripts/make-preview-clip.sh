#!/bin/sh
# Regenerates app/assets/raw/preview.pcm: the spoken "Preview." mixed into
# AirPlay audio every 5 s until the watermark is purchased.
#
# Voice: Piper (MIT) with en_US-ljspeech-high, trained on the public-domain
# LJ Speech dataset. Output: 44.1 kHz, mono, signed 16-bit little-endian, raw.
#
# Usage: scripts/make-preview-clip.sh <path to en_US-ljspeech-high.onnx>
set -eu
model=$1
out=$(dirname "$0")/../app/assets/raw/preview.pcm
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo 'Preview.' | uvx --python 3.12 --from piper-tts piper --model "$model" --output_file "$tmp/preview.wav"
ffmpeg -hide_banner -loglevel error -y -i "$tmp/preview.wav" \
  -af "loudnorm=I=-16:TP=-1.5:LRA=11,aresample=44100" -ac 1 -f s16le "$out"
echo "wrote $out ($(wc -c < "$out") bytes)"
