# Appstore assets

Everything the Amazon Developer Console's **Appstore Details** step asks for, and where it lives in this repo. The files go in `assets/` at the repository root. That's the store listing, not the app: the app's own bundled images are in `app/assets/` and `app/src/assets/`.

The listing rules come from `.claude/docs/PLAN.md` §1.3 and apply to every field below:

- **"AirPlay" appears once,** in the description, followed by "RosellaView is not affiliated with or endorsed by Apple Inc. AirPlay is a trademark of Apple Inc." Never "Works with Apple AirPlay". Never Apple's AirPlay glyph in any image, and never "AirPlay" in the title or icon.
- **Never "Vega"** in any field (Amazon's rule).
- **Apple's marks:** no ™ or ® symbols: the listing goes to every marketplace, and Apple's trademark list says symbols are only for publications distributed solely in the U.S.; elsewhere, use the attribution notice. So each description ends with Apple's attribution: "Apple, … are trademarks of Apple Inc., registered in the U.S. and other countries and regions.", listing the marks that field uses, alongside the non-affiliation line. Other companies' marks get their own line ("YouTube is a trademark of Google LLC."; for iOS, Apple's wording: "IOS is a trademark or registered trademark of Cisco in the U.S. and other countries and is used under license."). The app follows the same rule (About's Trademarks section).
- **Say "Based on UxPlay (GPLv3). Source code: https://github.com/regulad/RosellaView."** in the long description.

## Layout

```
assets/
  firetv-icon.png           Fire TV app icon (landscape)
  firetv-background.png     Fire TV background image
  firetv-screenshots/
    01-<what>.png           Screenshots, 3 to 10, in listing order
    02-<what>.png
    …
  tablet-icon-512.png       Tablet icon A
  tablet-icon-114.png       Tablet icon B
  tablet-screenshots ->     Symlink to firetv-screenshots: the console asks for
                            tablet assets even for a TV-only app, and these do
  listing/
    title.txt               Display title
    short-description.txt   Short description
    long-description.txt    Long description
    features.txt            Product feature bullets, one per line
    keywords.txt            Keywords, space-delimited, one line
  iap/
    in-app-items.csv        In-app items, for the console's CSV import
  sources/                  What the images are made from (firetv-icon.svg,
                            firetv-background.svg, tablet-icon.svg) and
                            render.sh, which renders them to the PNGs above
```

Platform-specific assets carry a `firetv-` or `tablet-` prefix. The text, the in-app items and the sources are shared.

The images are committed as the exact files to upload, so nothing has to be rebuilt at submission time. Their sources go in `sources/`. The logo and mark already live in `docs/brand/`; reference them rather than copying.

## Images

| File | Size | Format | Notes |
|---|---|---|---|
| `firetv-icon.png` | 1280 × 720 px | PNG, no transparency | Landscape, not square: it's the Fire TV home-screen tile. Original art only: no AirPlay glyph, no "AirPlay" wording. Fill the whole frame with an opaque background. |
| `firetv-screenshots/NN-*.png` | 1920 × 1080 px | PNG | 3 minimum, 10 maximum. Amazon also accepts JPG; we use PNG. Real captures of the app on a TV (`firetv-screenshots/README.md` says how). |
| `firetv-background.png` | 1920 × 1080 px | PNG | Shown behind the listing on Fire TV. JPG accepted; we use PNG. Keep the centre and left calm: the Appstore overlays the title, buttons and text there. |
| `tablet-icon-512.png` | 512 × 512 px | PNG | Tablet icon A (the console requires tablet assets even though RosellaView targets Fire TV only). The logo on white. |
| `tablet-icon-114.png` | 114 × 114 px | PNG | Tablet icon B, from the same source. |
| `tablet-screenshots/` | | | A symlink to `firetv-screenshots/`: the same files are uploaded for both. |

"No transparency" means no alpha channel at all, not just an opaque alpha channel. Export as RGB. To check: `file firetv-icon.png` should say `8-bit/color RGB`, not `RGBA`.

## Text

Limits are in characters (an emoji counts as one). Files are UTF-8 with no trailing newline beyond the last line.

| File | Limit | Notes |
|---|---|---|
| `listing/title.txt` | 200 | `RosellaView`. No "AirPlay", no "Vega", no "Fire TV". |
| `listing/short-description.txt` | 1,200 | Shown first; starts with "Receive AirPlay screen mirroring from iPhone, iPad and Mac." This is the one place "AirPlay" appears, with the non-affiliation and trademark lines right after it. |
| `listing/long-description.txt` | 4,000 | How to use it (same Wi-Fi network; Control Center → Screen Mirroring), what works and what may not (from `docs/COMPATIBILITY.md`), the watermark and its one-time purchase, privacy in one sentence with the policy URL, and the UxPlay/source line. No second "AirPlay": say "screen mirroring". |
| `listing/features.txt` | 10 lines | One feature per line, short (the console shows them as bullets). No "AirPlay". |
| `listing/keywords.txt` | Space-delimited | Search terms describing the function. No third-party trademarks ("AirPlay", "Apple", "iPhone", competitors' names): Amazon's IP policy, and the norm Apple's App Store spells out. |

## In-app items (`iap/in-app-items.csv`)

The Developer Console imports in-app items from CSV: **In-App Items → Import Multiple IAPs using CSV**, choosing TEST (Live App Testing) or TEST and LIVE. The format follows Amazon's Vega template (https://amzndevresources.com/vega/Amazon_IAP_Template.csv) and its field reference (https://developer.amazon.com/docs/in-app-purchasing/iap-bulk-items-csv.html):

- UTF-8, CRLF line endings, one header row:
  `Product ID (SKU),Locale; Title; Description,Auto Fill Prices,Price,IAP Type`
- **Product ID:** `rosellaview.remove_watermark`. It must match `UNLOCK_SKU` in `app/src/Purchases.ts`. It's unique per developer account and can't be changed or deleted once submitted.
- **Locale; Title; Description:** semicolon-separated sets ending in `;`. `en_US` is required. Keep commas and double quotes out of it: the file isn't quoted.
- **Auto Fill Prices `TRUE`** with **Price** `USD; 3990000`: $3.99, in millionths of the currency unit, with the other marketplaces filled in from it.
- **IAP Type:** `Entitlement` (a one-time unlock). It can't be changed after creation.

## Not in this folder

- **Privacy policy URL:** https://regulad.github.io/RosellaView/ (source `docs/PRIVACY.md`).
- **Privacy questionnaire and reviewer notes:** `.claude/docs/SUBMISSION.md`.
- **The package:** `rosellaview_armv7.vpkg` from the tag's GitHub Release.
