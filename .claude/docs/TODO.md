# TODO: getting users

RosellaView went live on the Amazon Appstore on 2026-11-12 (https://www.amazon.com/dp/B0HQVZR94M). The opening is that Vega devices (the Fire TV Stick 4K Select and newer) can't run Android apps, so AirScreen, AirReceiver and every other receiver that the "AirPlay to Firestick" guides recommend don't run on them. Most work here is about being found by Vega owners who try those guides and get stuck.

## 1. Appstore search (listing text: a console edit, no new build)

- [ ] **Title:** `RosellaView: Screen Mirroring for iPhone, iPad & Mac` (`assets/listing/title.txt`). The Apple product names come after our own name, the order Apple's guidelines ask for.
- [ ] **Keywords:** add `iphone ipad mac` (`assets/listing/keywords.txt`). They're referential, like the title.
- [ ] **`airplay` in keywords:** first check whether searching "airplay" on a Vega device already finds RosellaView. The short description says "Receive AirPlay screen mirroring", and the description may already be searchable. Add the keyword only if it doesn't, and keep AirPlay out of the title.
- [ ] Keep the Apple trademark notice in the description whenever the listing changes.
- [ ] Ask for a rating: one unobtrusive prompt after a few successful sessions. This is new UI, so it needs the owner's go-ahead (AGENTS.md: UI as small as possible).

## 2. Where stuck Select owners are

- [ ] Reddit (r/fireTV, r/amazonfiretv): reply to threads about AirScreen or AirPlay on the 4K Select, as the developer.
- [ ] Amazon's 4K Select product page, Q&A: "Can I mirror my iPhone?"
- [ ] Email the authors of the guides that rank for "AirPlay to Firestick" (Tom's Guide, smarttvs.org, firesticktricks, Eltima, PigeonCast). Their advice is wrong for every Vega device.

## 3. Amazon

- [ ] Pitch Appstore featuring, or the Amazon developer blog. Screen mirroring is a gap on Vega, and Amazon is investing in Vega apps (it sponsored OpenIAP's Vega support).

## 4. Technical audience

- [ ] Show HN: an open-source AirPlay receiver for Amazon's Linux-based Vega OS, with the Vega notes gist (https://gist.github.com/regulad/d06a96cfe3de8dcf0b13f5df629b9e0b).
- [ ] Ask UxPlay to list RosellaView among the projects built on its library.

## 5. Measuring

- [ ] Installs and purchases: the developer console's reports.
- [ ] Active use: Sentry session counts per release.

## Legal (overdue)

- [ ] **The lawyer review of playfair was due before the IAP went live** (PLAN.md §6). The IAP is live now. Ask the same lawyer about the trademark questions too:
  - "for iPhone, iPad & Mac" in the title
  - "airplay" as a keyword
  - "Receive AirPlay screen mirroring" in the description

  AirPlay is a licensed technology ("Works with Apple AirPlay"), and we aren't a licensee.
