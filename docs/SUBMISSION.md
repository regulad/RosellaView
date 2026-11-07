# Appstore submission checklist

The owner does the submission; nothing here is automated. Listing wording rules are in `PLAN.md` §1.3 and `AGENTS.md` (no "Vega" in listing text; no "AirPlay" or Apple's glyph in the app's name or icon).

## Before uploading

- [x] Development hooks removed (crash and error tests). The MSE video path stays as Compatibility Mode, which only the viewer can switch on.
- [x] App Tester removed from `app/manifest.toml`. In-app purchases can't be tested against App Tester with this manifest. To test them again, put back the `com.amazon.iap.tester.service` service and `/com.amazonappstore.iap.tester@IIAPTesterUI` module wants (see git history).
- [x] In-app About & privacy screen: AGPL §5(d) notices, third-party notices, trademark notice, privacy disclosure.
- [x] Source published at https://github.com/regulad/RosellaView (`SOURCE_URL` in `app/src/About.tsx`); the UxPlay fork is at https://github.com/regulad/UxPlay (branch `rosella`). Push before each release, so the shipped commit is public.
- [x] Privacy policy hosted at https://regulad.github.io/RosellaView/ (GitHub Pages, branch `gh-pages`, `index.md`). It's a copy of `docs/PRIVACY.md`: update both together. Enter the URL in the portal.
- [ ] Build a production release with `scripts/build.sh production`, with an increased `build-number` in the manifest.
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
- Known limitations from `COMPATIBILITY.md`: TikTok and other apps that hand off video, and HDCP-protected video.
