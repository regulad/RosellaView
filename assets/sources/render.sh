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
render firetv-icon.svg 1280 720 firetv-icon.png
render firetv-background.svg 1920 1080 firetv-background.png
render tablet-icon.svg 512 512 tablet-icon-512.png
render tablet-icon.svg 114 114 tablet-icon-114.png
file firetv-icon.png firetv-background.png tablet-icon-512.png tablet-icon-114.png
