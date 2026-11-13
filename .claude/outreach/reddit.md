# Reddit outreach: RosellaView (drafts for Parker to review and post by hand)

Researched 2026-11-12. Nothing was posted, voted on or messaged.

## Read this first: what I could and couldn't check

- **Reddit itself was unreachable from this session.** reddit.com and old.reddit.com
  are blocked to the fetch tool and to the browser extension, and the `.json`
  endpoints wouldn't load. Bing and DuckDuckGo then hit "are you human" checks,
  which I didn't solve. So the threads below come from **search-engine snippets
  only**. I didn't open them, read their comments or see whether they're
  archived, so **open each one before replying.**
- **Search engines barely index the Vega-era Reddit threads.** Results for
  "4K Select AirPlay", "4K Select AirScreen", "Vega OS screen mirroring" and the
  like were dominated by 2022–2024 Fire OS threads. Searching Reddit directly is
  your best source of targets. Links that should surface them:
  - https://old.reddit.com/r/fireTV/search?q=select+airplay&restrict_sr=on&sort=new&t=year
  - https://old.reddit.com/r/fireTV/search?q=select+airscreen&restrict_sr=on&sort=new&t=year
  - https://old.reddit.com/r/fireTV/search?q=vega+mirror&restrict_sr=on&sort=new&t=year
  - https://old.reddit.com/r/firetvstick/search?q=select+airplay&restrict_sr=on&sort=new&t=year
  - https://old.reddit.com/r/firestick/search?q=select+mirror&restrict_sr=on&sort=new&t=year
  - https://old.reddit.com/r/amazonfiretv/search?q=airplay&restrict_sr=on&sort=new&t=year
  - https://old.reddit.com/search?q=%224K+Select%22+iphone&sort=new&t=year
- **Archiving:** by default Reddit archives posts after 6 months, so anything
  posted before about 2026-05-12 is probably not replyable. Some subreddits turn
  archiving off, though, so check for the "Archived post" banner.
- **The Amazon Forum has the clearest live demand.** Its Fire TV threads asking
  exactly this question are recent and readable. They're listed in table B.
  They're not Reddit, but the same reply works there.

## 1. Threads

### A. Reddit (from search snippets; dates come from post IDs and context)

| # | URL | Sub | Date | Title | Archived? | Question |
|---|---|---|---|---|---|---|
| 1 | https://www.reddit.com/r/fireTV/comments/1pm01ng/does_this_still_have_the_android_os/ | r/fireTV | ~mid-Dec 2025 | Does this still have the Android OS? | Probably (older than 6 months). **Not replyable** unless archiving is off | A buyer checking whether the new stick still sideloads. Mirroring isn't mentioned, but it's the Vega confusion. |
| 2 | https://www.reddit.com/r/Stremio/comments/1pins7i/did_amazon_stop_side_loading_apps_on_fire_stick/ | r/Stremio | ~Dec 9 2025 | Did Amazon stop side loading apps on fire stick? | Probably. **Not replyable** | 4K Select owner can't sideload. Off-topic for mirroring: skip. |
| 3 | https://www.reddit.com/r/fireTV/comments/1cgqfi8/how_to_use_airplay_with_iphone_on_fire_tv_stick/ | r/fireTV | ~Apr 2024 | How to Use AirPlay with iPhone on Fire TV Stick 4K Max? | Yes. **Not replyable** | iPhone to 4K Max (Fire OS). Answered "use AirScreen", which is right for that device. |
| 4 | https://www.reddit.com/r/fireTV/comments/190l3ro/free_options_for_airplay/ | r/fireTV | ~Jan 2024 | Free Options For AirPlay? | Yes. **Not replyable** | Free AirPlay receivers on Fire OS 8. |
| 5 | https://www.reddit.com/r/fireTV/comments/yjbeu3/best_way_to_airplay_from_iphone_to_fire_tv/ | r/fireTV | ~Nov 2022 | Best Way To AirPlay From iPhone to Fire TV? | Yes. **Not replyable** | iPhone to 4K Max. |
| 6 | https://www.reddit.com/r/fireTV/comments/snhbhn/airplay_on_firetv_stick/ | r/fireTV | ~Feb 2022 | Airplay on FireTV stick | Yes (snippet says so). **Not replyable** | Can iOS AirPlay to a Fire TV stick? |

Rows 3–6 are the old threads that Google ranks for "AirPlay Fire TV". They
show the standard answer ("install AirScreen") that new Vega owners find and
can't follow. That's the gap a standalone post fills. **I found no recent,
replyable Reddit thread that asks the Vega mirroring question.** Use the search
links above to find live ones.

### B. Amazon Forum, Fire TV section (opened and read; recent, Vega-specific)

| # | URL | Device | Date | Title | Status | Question |
|---|---|---|---|---|---|---|
| B1 | https://www.amazonforum.com/s/question/0D5at00000y27CqCAI/where-is-the-airscreen-app | Fire TV Stick HD (newest, 2026, Vega) | 2026-10-11 | Where is the Airscreen app?! | Open, 2 answers, 41 views | Paid for AirScreen on the old stick; can't find it on the new HD stick. **Most promising.** |
| B2 | https://www.amazonforum.com/s/question/0D5at00000cIEl1CAG/airplay-not-compatible-with-fire-tv-stick-4k-select | 4K Select | 2026-01-17 | Airplay not compatible with Fire TV Stick 4K Select | Open, 2 answers, 707 views | Trying to mirror an iPad; AirScreen and other mirroring apps say "not compatible". Answers: a user says developers need to port apps, and Amazon staff posted generic Miracast steps that don't apply to iPad. **Answered wrongly or not at all.** |
| B3 | https://www.amazonforum.com/s/question/0D5at00000awYrXCAU/why-cant-i-see-my-airscreen-app | 4K Select | 2025-12-27 | Why Can´t i see my Airscreen app? | Open, 642 views | AirScreen is in their account but the 4K Select can't find it. A reply (per search snippet) explains the Select runs Vega. |

Replying on the Amazon Forum needs an Amazon login. Its pages warn about
third-party links, but an Amazon Appstore link is Amazon's own. I didn't find
written self-promotion rules there, so disclose that you're the developer, as
on Reddit.

Also worth knowing: a Japanese blog (tetsu7017.com, citing May 2026) says there
was "no practical way" to mirror over Wi-Fi to the HD (2026) or the 4K Select.
That fits the pitch.

## 2. Reply drafts

Facts used (from README, docs/COMPATIBILITY.md and assets/listing/*.txt, plus
the price you gave):
- It's free.
- A moving badge and an occasional spoken "Preview" appear until a one-time
  $3.99 purchase removes them.
- Phone and TV must be on the same Wi-Fi.
- You use Control Center → Screen Mirroring and pick the name shown on the TV.
- It plays sound, and audio-only works too.
- Copy-protected video (most paid streaming apps) won't show.
- Apps that "cast" instead of mirroring may show only their controls.
- Compatibility Mode exists for when the picture doesn't appear.
- It's open source.
- The only tested receiver is the 4K Select.

Don't claim that it works on the Fire TV Stick HD (2026). It hasn't been
tested there.

### Template (Reddit or Amazon Forum, for "how do I AirPlay / mirror my iPhone to a 4K Select")

> The 4K Select runs Amazon's new Vega OS instead of Android, so AirScreen,
> AirReceiver and the other usual answers aren't offered for it and can't be
> sideloaded.
>
> I'm the developer of one that is: RosellaView, a screen mirroring receiver
> made for these sticks. It went live on the Appstore today:
> https://www.amazon.com/dp/B0HQVZR94M
>
> Open it on the TV. Then on the iPhone/iPad, open Control Center → Screen
> Mirroring and pick the name the TV shows (on a Mac, Screen Mirroring in
> Control Center). Both need to be on the same Wi-Fi. Sound comes through too.
>
> It's free with a small moving watermark and an occasional spoken "Preview".
> A one-time $3.99 purchase removes both, with no subscription. Paid streaming
> apps (Netflix etc.) won't show, because iOS leaves copy-protected video out
> of mirroring. Photos, Safari, YouTube and so on work.

Notes:
- Keep the one link. If a sub bans links, drop it and write
  "search RosellaView in the Appstore".
- "Live on the Appstore today" only fits a post made on 2026-11-12. Delete it
  afterwards.

### B1 (Fire TV Stick HD 2026, "Where is the AirScreen app?!")

> The newest Fire TV Stick HD runs Amazon's new Vega OS instead of Android,
> so AirScreen isn't available for it (not something you set up wrong; it
> can't run there). Since you pay yearly for it, it may be worth asking
> AirScreen about a refund.
>
> I'm the developer of RosellaView, a screen mirroring receiver for iPhone,
> iPad and Mac built for Vega Fire TVs:
> https://www.amazon.com/dp/B0HQVZR94M. I've tested it on the 4K Select, not
> on the new HD yet. If the Appstore offers it to your stick I'd like to know
> how it goes.
>
> Free with a small moving watermark; a one-time $3.99 removes it.

### B2 (4K Select, iPad, "AirPlay not compatible")

> The 4K Select runs Vega OS rather than Android, so the AirScreen-type apps
> really are incompatible, and the "Enable Display Mirroring" setting is for
> Android/Windows, not iPad.
>
> I'm the developer of RosellaView, a screen mirroring receiver for these
> sticks: https://www.amazon.com/dp/B0HQVZR94M. Open it on the TV, then on the
> iPad: Control Center → Screen Mirroring → the name shown on the TV (same
> Wi-Fi). It's free with a moving watermark, and a one-time $3.99 removes it.
> Copy-protected video from paid streaming apps won't come through; most
> other things do.

### B3 (4K Select, "can't see my AirScreen app")

Use the template, trimmed to its first two paragraphs plus the price line.

## 3. Standalone post

**Where:** r/firetvstick. Its rules (per a third-party summary checked October
2026) say promotion needs moderator permission and that developer/app posts
with links appear to be tolerated. It's the only target sub with a stated route
for this. **Message the mods first** (draft below), and post only after a yes.
Second choice: r/fireTV, the largest of these subs. Its rules take no clear
position, so ask its mods the same way.

**Modmail draft:**

> Hi, I'm the developer of RosellaView, an open-source AirPlay screen-mirroring
> receiver for the Fire TV Stick 4K Select (Vega OS). AirScreen and similar
> apps don't run on Vega, and owners keep asking how to mirror iPhones. Would a
> single post announcing it, with developer disclosure, be OK here? Happy to
> follow whatever format you prefer.

**Title:** I made a free screen mirroring receiver for the Fire TV Stick 4K
Select (Vega), since AirScreen doesn't run on it

**Body:**

> If you have a 4K Select (or another Fire TV on Vega OS) you've probably
> noticed that AirScreen, AirReceiver and every other "mirror your iPhone"
> answer isn't available. Vega can't run Android apps and can't sideload.
>
> I'm the developer of RosellaView, a receiver built for Vega. It's on the
> Amazon Appstore as of today: https://www.amazon.com/dp/B0HQVZR94M
>
> **How it works:** open it on the TV, then on iPhone/iPad open Control Center
> → Screen Mirroring and pick the name shown on the TV. On a Mac, use Screen
> Mirroring in Control Center or the menu bar. Same Wi-Fi network; no account
> or app on your phone.
>
> **What works:** your whole screen in portrait or landscape with sound, up to
> 60 fps when the device sends it, video inside mirrored apps (Photos, Safari,
> YouTube), full-screen video from a Mac, and audio-only playback for music
> and podcasts. There's an adjustable audio delay (left/right on the remote)
> if lip sync is off.
>
> **What doesn't:** copy-protected video (most paid streaming apps) because
> iOS leaves it out of mirroring, and apps that "cast" rather than mirror
> (e.g. TikTok) may show only their controls. If you get no picture, try
> Compatibility Mode under About & privacy.
>
> **Cost:** free, with a small moving RosellaView badge and an occasional
> spoken "Preview". A one-time $3.99 purchase removes both. No subscription,
> no ads.
>
> It's open source (AGPL, built on UxPlay): https://github.com/regulad/RosellaView
>
> So far I've tested it on a 4K Select with an iPhone 17 Pro, an iPad mini 4
> and a 2015 MacBook. Bug reports here or on GitHub are very welcome.

(This has two links, the Appstore and the source. If the mods want only one,
keep the Appstore link and put the source in a comment. Don't title it "Works
with Apple AirPlay". "AirPlay" in passing in body text is fine.)

## 4. Subreddit rule notes

None of these is from the subreddit's own rules page; Reddit was
unreachable. The summaries are Thread Otter's (third party, automated, "last
checked October 2026"). Check each sub's /about/rules and sidebar before
posting.

| Subreddit | Self-promo rule (summary) | Reply with link? | Standalone post? |
|---|---|---|---|
| r/firetvstick | Promotion only with mod permission. Developer/app posts with links appear tolerated. Avoid copy-paste marketing. | Likely OK in an on-topic answer with disclosure | **Yes, after mod approval.** Best target. |
| r/fireTV | Written rules take no clear position; enforced by removals. No explicit link policy. | Probably OK as a direct answer with disclosure; vary the wording | Ask mods first |
| r/amazonfiretv | No clear position; "ask moderators before posting promotions or product links" | Cautious: maybe drop the link and say "search RosellaView" | Ask mods first |
| r/firestick | No clear rule; favours practical app recommendations. Bot note says r/firetvstick is more moderated. Heavy sideloading/IPTV crowd. | Probably OK as an answer | Low value; ask first |
| r/iphone | No clear rule; links from newer accounts get auto-removed | Only in a thread that names a Vega Fire TV | No |
| r/appletv | Not checked | Off-target (Apple TV owners don't need this) | No |

Reddit-wide norms: disclose that you're the developer; keep self-promotion
around 10% of your activity (the classic 9:1 guideline); don't paste
identical text in several subs on the same day; answer the question even for
people who won't install it.
