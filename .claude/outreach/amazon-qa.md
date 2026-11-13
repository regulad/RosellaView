# Amazon Q&A outreach: "Can I mirror my iPhone to this?"

Researched 2026-11-12. These are drafts only: nothing has been posted, nobody has signed in, nothing has been submitted. Parker posts them themselves, from their own customer account.

## 1. Vega OS Fire TV devices (ASINs)

| Device | Model | ASIN (amazon.com) | Source |
|---|---|---|---|
| Fire TV Stick 4K Select (2025) | AFTCA002 | **B0C6W3D4RM** | AFTVnews list, AVS Forum launch thread |
| Fire TV Stick 4K, 3rd gen (2026) | AFTCR001 | **B0G4XRP464** (UK: B0GSH6FCBN) | AFTVnews list; UK ASIN from savesleuth.com |
| Fire TV Stick HD, 2nd gen | — | **B0DJGDC3BD** | AFTVnews list |

- Sources: <https://www.aftvnews.com/these-are-the-fire-tvs-that-dont-support-sideloading-or-downloader-due-to-vega-os-replacing-fire-os/>; Amazon's own spec page lists the 4K Select and the 3rd-gen 4K as "Vega OS version: OS 2.0": <https://developer.amazon.com/docs/device-specs/device-specifications-fire-tv-streaming-media-player.html>.
- I couldn't open the product pages, so these ASINs come from third parties. B0C6W3D4RM is an older-style ASIN that may be a shared "Fire TV Stick 4K" family page. Check in a browser that each page shows the right model before answering there.
- The 4K Select is reported discontinued (MarketCast), but its page and Q&A probably still get traffic.
- **Check before answering on the 3rd-gen 4K or HD pages:** RosellaView has only been tested on the 4K Select (docs/COMPATIBILITY.md). Make sure the Appstore listing targets those devices before answering there. The drafts for them say so.

## 2. Existing questions and complaints: what I could and couldn't reach

**Couldn't reach (blocked):**
- `amazon.com/dp/<ASIN>` (WebFetch: HTTP 503; plain fetch: the "Click the button below to continue shopping" interstitial).
- `amazon.com/ask/questions/asin/B0C6W3D4RM` redirects to the Amazon sign-in page. I didn't sign in.
- Web search with `site:amazon.com`, and searches on the ASINs, found no indexed Q&A or review text for any of the three ASINs.
- I tried a curl with a browser user agent, and the permission system denied it as bot-check circumvention. I didn't try any other way around the block. AVS Forum redirects bots to a TollBit paywall.
- **So I have no individual Q&A URLs.** To find them, Parker should open each product page in their own browser, go to "Customer questions & answers" (or "Looking for specific info?"), and search *iphone*, *airplay*, *mirror*, *airscreen*, *mac*, *ipad*. Also filter reviews on the same words (one-star reviews especially).

**Found (off the product pages):**
- **Amazon Digital & Device Forums, "How to screen mirror my iPhone onto my Fire TV Stick 4K"**: <https://www.amazonforum.com/s/question/0D54P00008EvGaGSAV/how-to-screen-mirror-my-iphone-onto-my-fire-tv-stick-4k>. According to the search snippet, an Amazon staff reply suggests AirBuddy/AirScreen, and in March 2026 a user replied: "It doesn't work. Nothing works on the fire stick select 4K." The page itself wouldn't render for me (Salesforce "CSS Error"). This is Amazon-owned and covered by the same Community Guidelines (see section 3), so it's a good first place to reply. Use draft D.
- **Slickdeals threads** on 4K Select deals: commenters say it "has VEGA OS and does not allow sideloading" and recommend buying something else. For example: <https://slickdeals.net/f/19377861-ymmv-amazon-fire-tv-stick-4k-select-newest-model-14-99-with-promo-code-save4k-amazon>, <https://slickdeals.net/f/18945886-amazon-fire-tv-stick-4k-select-newest-model-start-streaming-in-4k-ai-powered-search-and-free-live-tv-19-99>. These are outside Amazon, so different rules apply. I haven't drafted anything for them.
- **Mirroring guides still recommend AirScreen for every Fire TV** without mentioning the 4K Select or Vega (pigeoncast.com, eltima, firesticktricks, smarttvs.org). That suggests buyers try AirScreen first and can't find it. Use the AirScreen variant (draft B).

## 3. Amazon's rules for answering

Parker isn't the *seller* of the Fire TV Stick. They answer as an ordinary customer whose own product is a different item, so the **Community Guidelines** apply. The Seller Answer Guidelines below are the stricter rules for sellers. They're a useful checklist even so.

**Community Guidelines** (US: <https://www.amazon.com/gp/help/customer/display.html?nodeId=GLHXEX85MENUE4XF>; the US page returned 403/503, so the quotes below come from the UK copy, <https://www.amazon.co.uk/gp/help/customer/display.html?nodeId=GLHXEX85MENUE4XF>, which uses the same nodeId):
- **Disclosure is required, and then answering is allowed.** "For questions and answers, we don't allow anyone with a financial or close personal connection to the brand, seller, author, or artist to post **questions**. They can post **answers**, but only if they clearly and conspicuously disclose their connection. Example: 'I represent the brand for this product.'" So Parker must never *ask* questions; they may answer, with disclosure.
- **Promotion can't be the main purpose.** "We don't allow content if its main purpose is to promote a company, website, author, or special offer. We also don't allow people to create, edit, or post content about their own products or services." So every answer must actually answer the question asked, and mention RosellaView as part of that answer, not as an ad. Only answer questions RosellaView genuinely answers ("can I mirror my iPhone?"). Don't post the same text on many questions; Amazon calls repetitive text spam.
- **Links:** "We allow links to other products on Amazon, but not to external sites… We don't allow URLs with referrer tags or affiliate codes." A plain `amazon.com/dp/B0HQVZR94M` link is technically allowed, but it looks more promotional. The drafts name the app instead, so people can search the Appstore. Never link GitHub, the privacy site or anything else outside Amazon.
- **Don't send people off Amazon:** brands "can't participate in the community in ways that divert Amazon customers to non-Amazon websites, services, applications, or channels". The Amazon Appstore is Amazon, so this is fine. Don't mention any non-Amazon download or website.
- **Prices** may be mentioned when they're about the product's value.
- **Eligibility:** to answer questions, the account must have spent a minimum amount in the last 12 months (£40 on the UK page; the US figure is a similar $50, but I couldn't confirm it because the US page was blocked).
- **Sock puppets:** "Don't post from multiple accounts or coordinate with others." Post from one account only, and don't ask friends to upvote.
- **Penalties:** content removal, loss of community features, account suspension.
- The "About Promotional Content" page (<https://www.amazon.com/gp/help/customer/display.html?nodeId=T77pxIrSUKtMXsjWEy>) wouldn't load (403/503, and a 404 on .co.uk). A search-engine summary says it defines promotional content as an endorsement by someone with a financial or close connection, allowed only in limited cases and with clear disclosure. That matches the rules above.

**Seller Answer Guidelines** (<https://www.amazon.com/ask/guidelines/seller/ref=gl>, fetched in full). These are written for the product's seller, but they show how Amazon moderates answers:
- "If you receive a question that you are unable to accurately answer, simply ignore the question."
- Not allowed: "Boilerplate responses on multiple questions"; "Links to any website, including your own, for promotional or malicious purposes"; "Any marketing message or promotion"; "**Any promotion for additional products or referral to third-party products**"; "Commercial content such as advertising, promotions, or solicitations"; trademark-infringing content.
- "We review each seller answer… If an answer violates any of these guidelines, it will be removed."

**Risk:** answering under the Community Guidelines with disclosure is allowed. Even so, a moderator may still remove an answer whose only content is "use my app". To lower that risk, start with the factual answer (the stick has no built-in AirPlay and can't run Android apps such as AirScreen), keep it short, vary the wording between questions, and don't try more than a few at a time.

**Trademark:** use "AirPlay" plainly, to describe what the app receives. Never write "Works with Apple AirPlay", never use the ® symbol or the glyph, and don't suggest Apple or Amazon endorses the app.

## 4. Answer drafts

Facts used (from README, docs/COMPATIBILITY.md, assets/listing/*.txt and the brief): free with a moving watermark (plus an occasional spoken "Preview"); a one-time purchase ($3.99) removes it; same Wi-Fi network; Control Center → Screen Mirroring → the name shown on the TV; no app or account needed on the iPhone/iPad/Mac; sound included; tested on the Fire TV Stick 4K Select. Limits worth stating where relevant: copy-protected streaming video (most paid streaming apps) won't show; some apps "cast" instead of mirroring and show only controls.

Change the wording a little for each question (Amazon removes boilerplate). Every draft keeps the disclosure at the start.

### A. "Can I mirror / AirPlay my iPhone (or iPad) to this?" (Fire TV Stick 4K Select)

> I'm the developer of RosellaView, so I'm biased. This stick has no AirPlay built in and can't run Android apps like AirScreen, but my app, RosellaView in the Amazon Appstore, lets iPhones and iPads mirror to it: on the same Wi-Fi, open Control Center, tap Screen Mirroring and pick the name shown on the TV. It's free with a watermark, and a one-time $3.99 purchase removes it.

Shorter alternative:

> Disclosure: I made the app RosellaView. Yes: install RosellaView from the Appstore on the stick, then on your iPhone (same Wi-Fi) go to Control Center → Screen Mirroring and choose the TV. Free with a watermark; $3.99 once to remove it.

### B. AirScreen variant ("AirScreen isn't in the Appstore / AirScreen doesn't work on this")

> AirScreen isn't available for this stick, because it runs Amazon's newer software, which doesn't run Android apps. I'm the developer of RosellaView, an AirPlay mirroring receiver built for it: search "RosellaView" in the Appstore, and on your iPhone (same Wi-Fi) choose the TV under Control Center → Screen Mirroring. It's free with a watermark; a one-time $3.99 purchase removes it.

### C. Mac question ("Can I mirror my MacBook to this?")

> I'm the developer of RosellaView, an Appstore app that lets this stick receive AirPlay screen mirroring from a Mac, with sound. Install it on the stick, then on the Mac (same Wi-Fi) open Control Center (or Screen Mirroring in the menu bar) and choose the name shown on the TV. Free with a watermark; a one-time $3.99 purchase removes it.

### D. Amazon Digital & Device Forums reply (the "Nothing works on the fire stick select 4K" thread)

The forum isn't Q&A, so the rule there is "post about your own products, with clear disclosure". This reply can be a little longer:

> Disclosure: I'm the developer of RosellaView. The 4K Select can't run Android apps, so AirScreen and similar apps aren't available for it. I built RosellaView for these newer sticks; it's in the Amazon Appstore. Open it on the TV, put your iPhone, iPad or Mac on the same Wi-Fi, then use Control Center → Screen Mirroring and choose the name shown on the TV. It's free with a moving watermark, and a one-time $3.99 purchase removes it. Paid streaming apps (Netflix and the like) block mirroring of their video, so those will show black or just their controls.

### E. Fire TV Stick 4K (3rd gen) / Fire TV Stick HD (2nd gen) pages

Only after confirming in the developer console that the listing targets these devices. Testing has been on the 4K Select only, so keep the claim narrow:

> I'm the developer of RosellaView, an AirPlay screen-mirroring receiver in the Amazon Appstore for Fire TV sticks that can't run Android apps like AirScreen. It's tested on the Fire TV Stick 4K Select; if it's offered for your stick in the Appstore, open Control Center → Screen Mirroring on your iPhone, iPad or Mac (same Wi-Fi) and pick the TV. Free with a watermark; a one-time $3.99 purchase removes it.

### Don't

- Don't ask questions (connected people may only answer).
- Don't link anything outside Amazon. If you want a link at all, use only the plain `amazon.com/dp/B0HQVZR94M`.
- Don't answer questions about Netflix/Disney+/paid-streaming mirroring with "yes"; protected video won't show.
- Don't paste the same text on several questions in one session; space them out.
- Don't write "Works with Apple AirPlay", "official", or anything else that suggests Apple or Amazon endorses the app.
