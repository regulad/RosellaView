# Show HN draft: RosellaView

Draft for Parker to review and post.  Nothing has been posted. Facts come from README.md, docs/COMPATIBILITY.md, docs/PRIVACY.md, assets/listing/*.txt, .claude/docs/DECISIONS.md (D1, D4, D9, D11–D13), .claude/docs/AIRPLAY.md and ProjectRosella/VEGA.md (the gist). Check the bracketed notes before posting.

## 1. Title options (80 characters max)

1. **`Show HN: RosellaView – AirPlay receiver for Fire TV's Linux-based Vega OS`** (73 characters). My pick. It says what the app is and where it runs, and "Linux-based Vega OS" is the hook for HN readers, who mostly haven't heard that Fire TV dropped Android.
2. `Show HN: An open-source AirPlay receiver for Amazon's Linux-based Fire TV OS` (76). Leads with open source, but leaves out the name.
3. `Show HN: RosellaView – mirror an iPhone or Mac to the new Vega Fire TV sticks` (77). Leaves "AirPlay" out of the title, if you'd rather keep the trademark only in the body.

All three avoid hype words, and none says "Works with Apple AirPlay". "AirPlay receiver" is used only descriptively.

## 2. URL to submit

**Submit the repo: https://github.com/regulad/RosellaView**

- HN readers reward source and engineering detail. The README links the gist, the compatibility list and the privacy policy, and DECISIONS.md holds the measurements.
- The Amazon listing reads as a store page, which on HN looks like marketing, and people without a Vega device can see nothing on it.
- Show HN requires "something people can try". Trying it needs a Vega device either way, so put the store link in the first line of the text, for people who have one.
- Save the gist ("Vega OS: what we've learned") for a separate, ordinary submission (not Show HN, because it's reading material) a week or more later. It's the most HN-friendly thing you have, and it shouldn't take attention from this post.

## 3. Text (put it in the submission's text box; if it doesn't show, post it as the first comment)

> I built RosellaView, an AirPlay screen-mirroring receiver for Vega OS, the Linux-based OS that Amazon now ships on Fire TV devices such as the Fire TV Stick 4K Select. Vega doesn't run Android apps, so AirScreen, AirReceiver and the other receivers that every "AirPlay to Firestick" guide recommends aren't available on it. You open RosellaView on the TV and choose it under Screen Mirroring on an iPhone, iPad or Mac. It's on the Amazon Appstore: https://www.amazon.com/dp/B0HQVZR94M
>
> The protocol side is UxPlay's library. Most of the work was getting video onto the screen of a 1 GB armv7 stick:
>
> - The documented route is the W3C MSE player. It keeps a fixed buffer of about 0.45 s. It treats any frame longer than twice the SPS's declared frame duration as a discontinuity and drops everything until the next keyframe, which iOS sends about once per session. And it decodes iOS's full-range video as limited range, so shades 0–16 and 235–255 merge.
> - So video now goes to a native view that renders with its own GLES. Decoding uses media_transform, a hardware decoder API that works from apps but isn't in the OS's compatibility contract. It returns MediaTek MM21-tiled frames, which I de-tile on the CPU in about 3.7 ms per 1080p frame (around 22% of one core at 60 fps; FFmpeg's software decoder would need about 1.2 cores).
> - Audio goes to the OS's PCM stream. Picture and sound both play 150 ms after capture, and I measure under 0.4 s end to end.
>
> Limits: copy-protected video in paid streaming apps doesn't appear, because iOS leaves it out of the mirror. Apps like TikTok that "cast" their video show only their controls, because I don't support AirPlay video handoff.
>
> It's free with a moving watermark; a one-time $3.99 purchase removes it. It's AGPL-3.0-or-later. It sends crash reports and diagnostics to Sentry, including the sender's model and OS version but no device names or IDs.
>
> Notes on developing for Vega: https://gist.github.com/regulad/d06a96cfe3de8dcf0b13f5df629b9e0b
>
> RosellaView isn't affiliated with Apple.

(About 330 words with the links, about 290 without. If it needs to be shorter, cut the audio bullet first.)

**[Check:]** "under 0.4 s" is from your own observation in D13. Say how you measured it if you can (for example, filming both screens with a stopwatch), because someone will ask.

## 4. Likely questions and suggested answers

**Isn't AGPL software in Amazon's Appstore a GPL problem? (the VLC/App Store argument)**
> The whole app is AGPL-3.0-or-later and the full source is public, including the UxPlay fork (github.com/regulad/UxPlay, branch `rosella`). Each release is built by CI from a tag, and the .vpkg is attached to the GitHub Release. The About screen shows the licence, third-party notices and the source URL. The native module is a plain Node-API addon with no Amazon code in it. The SDK's generated glue for calling OS libraries (APMF) is treated as a System Library under GPLv3 §1, like OS SDK headers; the reasoning is in DECISIONS.md D10. [Check whether you set a custom EULA that defers to the AGPL and turned Appstore DRM off, as PLAN.md §2.1 planned, and mention it if so. Only claim what you actually did.]

**So I can build it without the watermark?**
> Yes, the licence allows that. You'd need the Vega SDK and a device in developer mode. The purchase is for people who just want it to work from the Appstore. [Honest and HN-friendly: don't be defensive here.]

**"Vega can't sideload": what about developer mode?**
> Developer mode exists, and that's how I develop. But it needs the SDK, so it isn't a route for ordinary owners, and Android APKs don't run on Vega at all, which is why the existing receivers aren't available.

**What about playfair / FairPlay? Is this legal?**
> Like UxPlay, RPiPlay and the other open receivers, it includes UxPlay's `playfair` code (GPLv3), which handles the FairPlay key exchange that AirPlay mirroring uses. It doesn't touch DRM-protected content: iOS doesn't send protected video when mirroring. [Your legal review of playfair is still open (TODO.md). Don't claim it has been cleared. "Same approach as every other open AirPlay receiver" is accurate. Consider not going further than that in public.]

**Why not Chromecast / Google Cast / Miracast?**
> iPhones and Macs only mirror the whole screen over AirPlay. Google Cast from iOS works only per app, and Fire TV doesn't support it. This app exists for Apple devices; Android phones aren't supported. [Don't make claims about Vega's built-in Miracast support unless you've checked it on the stick.]

**Why charge? Why $3.99?**
> It's free to use with a watermark, so you can check that it works on your network before paying anything. The one-time $3.99 purchase removes the watermark and the occasional spoken "Preview". No subscription and no account.

**Telemetry? What leaves my network?**
> Mirrored video and audio go only from your device to the TV. Nothing is recorded or uploaded. The app sends crash reports and diagnostics to Sentry: errors, playback and performance statistics (frame rates, buffering, A/V sync, memory, CPU), the app version and the Fire TV's model and software version, the sending device's model and OS version (e.g. "iPhone18,1, iOS 27"), and connect/disconnect events. No device names, device IDs, or MAC or IP addresses, and the Sentry project is set not to store IPs. Data is kept for up to 90 days. Policy: https://regulad.github.io/RosellaView/ [There's no opt-out in the app. If someone asks for one, say so plainly. The answer is that no Vega crash reporter exists, so this is the only way you see field crashes (VEGA.md, "Crash reporting").]

**Latency? Can I game on it?**
> About 150 ms target, under 0.4 s end to end as I measured it. Fine for photos, video and presentations, but noticeable for fast games. Bad LAN/Wi-Fi Mode (in About) raises the target to 700 ms for lossy networks.

**Isn't relying on an off-contract API risky?**
> Yes. An OS update could break media_transform. The MSE path is still there as Compatibility Mode (About & privacy), and the fallback plan is FFmpeg software decoding, which keeps full range but costs about a core.

**Which devices work?**
> Tested on a Fire TV Stick 4K Select (OS 2.0) with an iPhone 17 Pro (iOS 27.0.1), an iPad mini 4 (iPadOS 15.8.8) and a 12-inch MacBook (macOS 11.7.11). Other Vega devices should work, but I haven't tested them; reports welcome on GitHub issues.

**Did you use AI to write this?** (likely: the repo has CLAUDE.md and `.claude/docs/`)
> [Answer in your own words, briefly and honestly. HN reacts better to a straight answer than to finding out on its own. The measurements in DECISIONS.md, all taken on the device, are good evidence that the engineering is real.]

## 5. Timing

- **Best:** Tuesday to Thursday, around 8–10 a.m. US Eastern (13:00–15:00 UTC). That's when US readers arrive and Europe is still online. Weekend mornings have less competition but fewer readers, and for a niche post that can work just as well.
- **Be available** for the first 2–3 hours to answer comments; Show HN expects it.
- **Don't ask anyone to upvote or comment.** HN's rules forbid it, and its voting-ring detection penalises it.
- If it doesn't get traction, you can repost once after a few days; HN allows that. Or send a short note to hn@ycombinator.com, which sometimes puts good Show HNs into the second-chance pool.
- Post it after the store listing's title and keywords update (TODO.md §1) has gone live, so visitors from HN see the final listing.
