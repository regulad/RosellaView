# Appstore submission checklist

The owner does the submission; nothing here is automated. Listing wording rules are in `PLAN.md` §1.3 and `AGENTS.md` (no "Vega" in listing text; no "AirPlay" or Apple's glyph in the app's name or icon).

## Before uploading

- [x] Development hooks removed (crash and error tests). The MSE video path stays as Compatibility Mode, which only the viewer can switch on.
- [x] App Tester is out of the committed manifest. `scripts/build.sh` adds its two wants to development builds only, so sandbox purchases keep working there; `scripts/build.sh production` leaves them out.
- [x] In-app About & privacy screen: AGPL §5(d) notices, third-party notices, trademark notice, privacy disclosure.
- [x] Source published at https://github.com/regulad/RosellaView (`SOURCE_URL` in `app/src/About.tsx`); the UxPlay fork is at https://github.com/regulad/UxPlay (branch `rosella`). Push before each release, so the shipped commit is public.
- [x] Privacy policy hosted at https://regulad.github.io/RosellaView/ (GitHub Pages, branch `gh-pages`, `index.md`). It's a copy of `docs/PRIVACY.md`: update both together. Enter the URL in the portal.
- [ ] Release build, by tag (`.github/workflows/release.yml`). Releases are identified by the build number alone, the only number the Appstore uses. It's the manifest's `version`, a plain integer; `scripts/build.sh` passes it to the packager as `--build-number`, and stamps `rosellaview@<build>` for Sentry:
  1. Run `scripts/bump.sh` (next build; or `scripts/bump.sh <build>`). It sets the manifest's `version`, `app/package.json`'s `<build>.0.0` (the Appstore may read it: build 3 showed as package.json's 0.2.0), and the ad-hoc `rosellaview@<build>+dev` in `app/src/buildInfo.ts`. `scripts/build.sh` refuses a package.json that disagrees. Builds 1-3 had a separate `build-number` and 0.x version names.
  - Production builds need `app/assets/raw/AppstoreAuthenticationKey.pem`, the app's public key (developer console: Upload Your App File > Additional information > View public key). Without it the purchase screen never opens: purchase() returns code 4. That failed build 3's review.
  2. Commit and push, then tag the build number: `git tag 4 && git push origin 4`.
  3. CI checks that the tag equals the manifest's `version`, runs the checks and builds with the pinned SDK (`VEGA_SDK_VERSION`). Source maps and debug files go to Sentry (secret `SENTRY_AUTH_TOKEN`, an organization token) as release `rosellaview@<build>`, and the `.vpkg` to the GitHub Release for the tag.
  4. Upload `rosellaview_armv7.vpkg` in the Developer Console by hand. It's the Fire TV Stick build, and Amazon's build docs name `armv7-release/*_armv7.vpkg` as the one to submit.

### Automating the upload: waiting on Amazon

The App Submission API (edits, `apks/upload`) can't take Vega packages yet, so CI stops at the GitHub Release. Status as of 2026-11-08:
- **The API docs** say "The API supports Android apps only" (overview last updated 2023-10-02). Nothing in the reference mentions VPKG.
- **Amazon staff** in the developer forum (topics 24910 and 27291): not supported (Dec 2025, Jan and Mar 2026). On **2026-08-10**: support is "planned to be available by end of 2026".
- **Other projects:** no public project uploads a `.vpkg` through the API yet. openhamclock/hamclock sends one to `apks/upload` (`.github/scripts/upload_amazon_appstore.py`, added 2026-11-03), but its only run since uploaded just the APK.

**Revisit when** the App Submission API docs or release notes officially document Vega/VPKG. Then add an upload step to `release.yml`: an LWA token, a new edit, the `.vpkg` upload, then commit or leave as a draft. hamclock's script is a model. It needs a Login with Amazon security profile; its client ID and secret, plus the app ID, become repository secrets.
  - Sentry release names: tagged builds are `rosellaview@<build>`; other builds get `+<sha>` (and `-dirty.<time>` if uncommitted). Builds 1 and 2 predate this: `rosellaview@0.1.0+95fd4e1b7866` (tag `v0.1.0`) and `rosellaview@0.1.1` (tag `v0.1.1`).
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
