# Screenshots

3 to 10 PNGs, 1920 × 1080, named `01-<what>.png`, `02-<what>.png`… in listing order (`docs/ASSETS.md`).

Capture them from the Fire TV Stick itself; the result is the real 1920×1080 output, RGB, ready to commit:

    vega exec vda -s 10.42.24.48:5555 shell 'gwsi-tool-screenshooter /tmp/shot.png'
    vega exec vda -s 10.42.24.48:5555 pull /tmp/shot.png assets/screenshots/NN-<what>.png

(The plain `screenshooter` tool fails; the Virtual Device supports neither.)
