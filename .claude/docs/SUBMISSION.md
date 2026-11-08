# Appstore submission checklist

The owner does the submission; nothing here is automated. Listing wording rules are in `PLAN.md` §1.3 and `AGENTS.md` (no "Vega" in listing text; no "AirPlay" or Apple's glyph in the app's name or icon).

## Before uploading

- [x] Development hooks removed (crash and error tests). The MSE video path stays as Compatibility Mode, which only the viewer can switch on.
- [x] App Tester is out of the committed manifest. `scripts/build.sh` adds its two wants to development builds only, so sandbox purchases keep working there; `scripts/build.sh production` leaves them out.
- [x] In-app About & privacy screen: AGPL §5(d) notices, third-party notices, trademark notice, privacy disclosure.
- [x] Source published at https://github.com/regulad/RosellaView (`SOURCE_URL` in `app/src/About.tsx`); the UxPlay fork is at https://github.com/regulad/UxPlay (branch `rosella`). Push before each release, so the shipped commit is public.
- [x] Privacy policy hosted at https://regulad.github.io/RosellaView/ (GitHub Pages, branch `gh-pages`, `index.md`). It's a copy of `docs/PRIVACY.md`: update both together. Enter the URL in the portal.
- [ ] Release build, by tag (`.github/workflows/release.yml`):
  1. Bump `version` in `app/package.json` and `app/manifest.toml`, and `build-number` in the manifest.
  2. Commit and push, then `git tag vX.Y.Z && git push origin vX.Y.Z`.
  3. CI runs the checks and builds with the pinned SDK (`VEGA_SDK_VERSION`, about 12 min). Source maps and debug files go to Sentry (secret `SENTRY_AUTH_TOKEN`, an organization token), and the `.vpkg` files to the GitHub Release for the tag.
  4. Upload `rosellaview_armv7.vpkg`, the only one, in the Developer Console. It's the Fire TV Stick build, and Amazon's build docs name `armv7-release/*_armv7.vpkg` as the one to submit. `x86_64` and `aarch64` are for the Vega Virtual Device only. The Appstore's submission API doesn't take Vega packages yet.
  - Sentry release names: tagged builds are `rosellaview@<version>`; any other build gets `+<sha>` (and `-dirty.<time>` if uncommitted). v0.1.0 predates this and is `rosellaview@0.1.0+95fd4e1b7866`.
  - A manual run (Actions → Release → Run workflow) is a dry run: it keeps the packages as an artifact, with no release and no Sentry upload.
- [ ] Lawyer review: playfair (GPLv3) and DECISIONS.md D10 (APMF glue as a System Library).

## Privacy questionnaire (suggested answers)

- **Does the app collect user data?** Yes.
- **Data types:**
  - **Crash logs and diagnostics:** yes. Errors, crash reports, performance and playback statistics.
  - **Device or other IDs:** no. Device names, AirPlay IDs and MAC/IP addresses are kept off Sentry (Telemetry.ts, receiver.c), and the Sentry project doesn't store IP addresses.
  - **Device information:** the sender's model and OS version; the TV's model and software version. Not linked to a person.
  - **Purchase history:** no. Amazon handles purchases.
  - **Audio, video, photos, screen content:** no. Mirrored content isn't recorded or transmitted off the local network.
  - **Location, contacts, health, financial, browsing:** no.
- **Purpose:** app functionality and analytics (diagnostics) only. No advertising, no sale, no sharing beyond the processor.
- **Third party:** Sentry (Functional Software, Inc.), as processor.
- **Encrypted in transit:** yes (HTTPS).
- **Deletion:** nothing is linked to a person, so there's nothing to request deletion of; questions by email.
- **Children:** not directed at children.

## Listing notes

- Mention the watermark and the one-time purchase that removes it.
- Known limitations from `docs/COMPATIBILITY.md`: TikTok and other apps that hand off video, and HDCP-protected video.
