# Appstore assets

Everything the Amazon Developer Console's **Appstore Details** step asks for, and where it lives in this repo. The files go in `assets/` at the repository root. That's the store listing, not the app: the app's own bundled images are in `app/assets/` and `app/src/assets/`.

The listing rules come from `.claude/docs/PLAN.md` §1.3 and apply to every field below:

- **"AirPlay" appears once,** in the description, followed by "RosellaView is not affiliated with or endorsed by Apple Inc. AirPlay is a trademark of Apple Inc." Never "Works with Apple AirPlay". Never Apple's AirPlay glyph in any image, and never "AirPlay" in the title or icon.
- **Never "Vega"** in any field (Amazon's rule).
- **Say "Based on UxPlay (GPLv3). Source code: https://github.com/regulad/RosellaView."** in the long description.

## Layout

```
assets/
  icon.png                  App icon
  background.png            Background image
  screenshots/
    01-<what>.png           Screenshots, 3 to 10, in listing order
    02-<what>.png
    03-<what>.png
  listing/
    title.txt               Display title
    short-description.txt   Short description
    long-description.txt    Long description
    features.txt            Product feature bullets, one per line
    keywords.txt            Keywords, space-delimited, one line
  sources/                  What the images are made from (SVG, layered files)
```

The images are committed as the exact files to upload, so nothing has to be rebuilt at submission time. Their sources go in `sources/`. The logo and mark already live in `docs/brand/`; reference them rather than copying.

## Images

| File | Size | Format | Notes |
|---|---|---|---|
| `icon.png` | 1280 × 720 px | PNG, no transparency | Landscape, not square: it's the Fire TV home-screen tile. Original art only: no AirPlay glyph, no "AirPlay" wording. Fill the whole frame with an opaque background. |
| `screenshots/NN-*.png` | 1920 × 1080 px | PNG | 3 minimum, 10 maximum. Amazon also accepts JPG; we use PNG. Real captures of the app on a TV. A mirrored iPhone or Mac screen is fine, but avoid showing other apps' copyrighted content, and don't show Apple's AirPlay glyph from the sender's UI. |
| `background.png` | 1920 × 1080 px | PNG | Shown behind the listing on Fire TV. JPG accepted; we use PNG. Keep the centre and left calm: the Appstore overlays the title, buttons and text there. |

"No transparency" means no alpha channel at all, not just an opaque alpha channel. Export as RGB. To check: `file icon.png` should say `8-bit/color RGB`, not `RGBA`.

## Text

Limits are in characters (a "®" or an emoji counts as one). Files are UTF-8 with no trailing newline beyond the last line.

| File | Limit | Notes |
|---|---|---|
| `listing/title.txt` | 200 | `RosellaView`. No "AirPlay", no "Vega", no "Fire TV". |
| `listing/short-description.txt` | 1,200 | Shown first; starts with "Receive AirPlay® screen mirroring from iPhone, iPad and Mac." This is the one place "AirPlay" appears, with the non-affiliation and trademark lines right after it. |
| `listing/long-description.txt` | 4,000 | How to use it (same Wi-Fi network; Control Center → Screen Mirroring), what works and what may not (from `docs/COMPATIBILITY.md`), the watermark and its one-time purchase, privacy in one sentence with the policy URL, and the UxPlay/source line. No second "AirPlay": say "screen mirroring". |
| `listing/features.txt` | 10 lines | One feature per line, short (the console shows them as bullets). No "AirPlay". |
| `listing/keywords.txt` | Space-delimited | Search terms. Pick them after looking at what comparable Fire TV mirroring apps use (to do). No competitor or Apple trademarks used as keywords beyond what the description allows. |

## Not in this folder

- **Privacy policy URL:** https://regulad.github.io/RosellaView/ (source `docs/PRIVACY.md`).
- **Privacy questionnaire and reviewer notes:** `.claude/docs/SUBMISSION.md`.
- **The in-app item:** an entitlement with SKU `rosellaview.remove_watermark` (it must match `app/src/Purchases.ts`).
- **The package:** `rosellaview_armv7.vpkg` from the tag's GitHub Release.
