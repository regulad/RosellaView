---
name: find-questions
description: Find public questions from Vega OS Fire TV owners (Fire TV Stick 4K Select, HD 2nd gen, 4K 3rd gen…) who can't mirror an iPhone, iPad or Mac, where answering with RosellaView would genuinely help, and draft disclosed answers for the owner to post. Use when asked to find places to plug, answer or promote RosellaView, or to refresh the outreach list. Never posts anything.
---

# Find questions worth answering with RosellaView

RosellaView is the AirPlay receiver for Vega OS (https://www.amazon.com/dp/B0HQVZR94M, source https://github.com/regulad/RosellaView). Vega devices can't run Android apps, so AirScreen, AirReceiver and the other receivers that guides recommend aren't available on them. Owners who follow those guides get stuck and ask for help in public. Find those questions and draft answers that solve them. The owner reviews and posts every answer by hand.

**Never post, comment, vote, message, sign in, or solve a CAPTCHA.** Research and draft only. If a site blocks you (bot checks, sign-in walls), record that and move on. Don't try to get around it, for example with spoofed user agents.

## 1. What makes a good target

- The asker has a **Vega** device: the Fire TV Stick 4K Select, the Fire TV Stick HD (2nd gen, 2026), the Fire TV Stick 4K (3rd gen), or "new" sticks where AirScreen is "not compatible", "not showing" or missing. Fire OS devices (4K Max, Cube, older sticks) already have AirScreen, so skip them.
- They want to mirror from an **iPhone, iPad or Mac**. Android phones aren't supported: skip them.
- **It's still open:** not locked or archived. Reddit archives threads after about 6 months. A recent thread or a high view count beats an old one. A wrong answer ("install AirScreen") or an Amazon staff answer that doesn't fit makes it a better target.
- **It hasn't been answered already.** Check `.claude/outreach/answered.md` (URLs the owner has posted to). Never suggest a second answer in the same thread.

## 2. Where to look, and what's reachable (as of 2026-11-12)

| Source | Reachable? | How |
| --- | --- | --- |
| Amazon device forum (amazonforum.com) | Pages often don't render, but search snippets work | Web search `site:amazonforum.com` with the queries below. This was the best source last time: three open 4K Select / HD threads. |
| Reddit (r/fireTV, r/amazonfiretv, r/firetvstick) | **Blocked** to fetch and browser, `.json` included | Web search `site:reddit.com …` gives titles and dates only. Hand the owner `https://old.reddit.com/r/<sub>/search?q=<query>&restrict_sr=on&sort=new` links to check. |
| Amazon product Q&A and reviews for Vega devices | **Blocked** (503, bot page, sign-in) | List the ASINs and tell the owner what to search on each page while signed in: *iphone, ipad, mac, airplay, mirror, airscreen*. Check the ASINs in a browser first; third-party sites disagree about them. |
| Slickdeals deal threads for Vega sticks | Reachable | Comments say "Vega, no sideloading, buy something else". Same audience; their rules differ from Amazon's. |
| Apple Community (discussions.apple.com) | Reachable | "Mirror iPhone to Fire TV Stick 4K Select" threads |
| Guides that rank for "AirPlay to Firestick" | Reachable | Not answer targets, but note authors whose advice is wrong for Vega (TODO.md §2, email). |

Bing and DuckDuckGo may show "are you human" checks. Use the search tool and note anything it couldn't reach.

**Queries:** `"4K Select" airplay`, `"4K Select" AirScreen`, `"4K Select" mirror iPhone`, `"Fire TV Stick HD" AirScreen 2026`, `Vega OS screen mirroring`, `AirScreen "not compatible" fire tv`, `fire stick select mirror ipad`, `firestick "vega" airplay`. Add the current year, and sort by newest where you can.

## 3. The rules for answers

- **Disclose first:** "I'm the developer of RosellaView." Amazon's Community Guidelines allow someone with a financial connection to *answer* only with clear disclosure, and never to *ask*. Most subreddits require the same, and r/firetvstick wants moderator approval for promotion, so check each sidebar.
- **Answer the question first.** For example: Vega sticks can't install AirScreen, and here's what works. Then the app, then one line on how to use it: same Wi-Fi network, then Control Center → Screen Mirroring → the name shown on the TV. On a Mac it's Control Center → Screen Mirroring.
- **Be honest about the price:** free with a moving watermark and a spoken "Preview" every few seconds; a one-time $3.99 purchase removes both.
- **Claim only what the repo says.** Use `docs/COMPATIBILITY.md` for what works and what doesn't: copy-protected video doesn't appear, and apps like TikTok that cast show only their controls. It has only been tested on the 4K Select, so for other Vega devices say "should work; only tested on the 4K Select".
- **Links:** none on Amazon product Q&A; say "search RosellaView in the Appstore". Elsewhere, at most one: the store page.
- **Trademark** (`docs/ASSETS.md`): use "AirPlay" plainly, to describe what it receives. Never "Works with Apple AirPlay", no ® or Apple glyphs, and no hint that Apple or Amazon endorses it.
- **Short and in the owner's voice.** Vary the wording between answers: identical text across threads gets removed as spam.

## 4. Output

Write or refresh `.claude/outreach/questions.md`. Older drafts are in the same folder: `reddit.md`, `amazon-qa.md` and `show-hn.md`.

1. **A table of targets, best first:**

   | URL | Site | Date | Device | Status | Question (one line) |
   | --- | --- | --- | --- | --- | --- |

   Status is one of: open, archived, locked, already answered, or unreachable (with what you could see).
2. **A draft per target,** under a heading with its URL, following the rules in §3.
3. **What you couldn't reach,** and the manual searches the owner should run. List URLs ready to click.

Finish by telling the owner the two or three best targets and why. Remind them to add each URL they post to to `.claude/outreach/answered.md`.
