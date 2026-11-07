# Appstore submission checklist

The owner does the submission; nothing here is automated. Listing wording rules are in `PLAN.md` §1.3 and `AGENTS.md` (no "Vega" in listing text; no "AirPlay" or Apple's glyph in the app's name or icon).

## Before uploading

- [x] Development hooks removed (crash and error tests). Compatibility Mode, the MSE fallback, stays as a user setting.
- [x] App Tester removed from `app/manifest.toml`. In-app purchases can't be tested against App Tester with this manifest. To test them again, put back the `com.amazon.iap.tester.service` service and `/com.amazonappstore.iap.tester@IIAPTesterUI` module wants (see git history).
- [x] In-app About & privacy screen: AGPL §5(d) notices, third-party notices, trademark notice, privacy disclosure.
- [ ] Publish the source at the URL shown in the app (`SOURCE_URL` in `app/src/About.tsx`, currently `https://github.com/regulad/RosellaView`), including the `regulad/UxPlay` fork.
- [ ] Host `docs/PRIVACY.md` at a public URL and enter it in the portal.
- [ ] Build a production release with `scripts/build.sh production`, with an increased `build-number` in the manifest.
- [ ] Lawyer review: playfair (GPLv3) and DECISIONS.md D10 (APMF glue as a System Library).

## Privacy questionnaire (suggested answers)

- **Does the app collect user data?** Yes.
- **Data types:**
  - **Crash logs and diagnostics:** yes. Errors, crash reports, performance and playback statistics.
  - **Device or other IDs:** yes. The sender's AirPlay device ID and MAC address.
  - **Device information:** the sender's name, model and OS version; the TV's model and software version.
  - **Purchase history:** no. Amazon handles purchases.
  - **Audio, video, photos, screen content:** no. Mirrored content isn't recorded or transmitted off the local network.
  - **Location, contacts, health, financial, browsing:** no.
- **Purpose:** app functionality and analytics (diagnostics) only. No advertising, no sale, no sharing beyond the processor.
- **Third party:** Sentry (Functional Software, Inc.), as processor.
- **Encrypted in transit:** yes (HTTPS).
- **Deletion:** on request by email (see the privacy policy).
- **Children:** not directed at children.

## Listing notes

- Mention the watermark and the one-time purchase that removes it.
- Known limitations from `COMPATIBILITY.md`: TikTok and other apps that hand off video, and HDCP-protected video.
