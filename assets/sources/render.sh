#!/usr/bin/env bash
# Renders the listing images from their SVG sources (docs/ASSETS.md): RGB PNGs
# with no alpha channel, at the sizes the Appstore asks for. Needs rsvg-convert
# and ImageMagick (magick), and the Noto Sans font for the wordmark.
#
# Copyright (C) 2026 Parker Wahle
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
render() { # source width height output
  rsvg-convert -w "$2" -h "$3" "sources/$1" | magick - -background white -alpha remove -alpha off -strip "PNG24:$4"
}
render icon.svg 1280 720 icon.png
render background.svg 1920 1080 background.png
file icon.png background.png
