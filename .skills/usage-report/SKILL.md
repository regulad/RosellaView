---
name: usage-report
description: Count RosellaView's users and summarize how they use it (installs, launches, connections, mirroring time, sender devices, purchases, failed or cut-short sessions) from what the app already sends to Sentry. Use when asked how many users there are, for analytics or usage numbers, or how a store build is doing. Read-only.
---

# Usage report from Sentry

RosellaView sends no user fields (`dataCollection: {userInfo: false}` in `app/src/Sentry.ts`) and no device IDs, as `docs/PRIVACY.md` promises. So Sentry's "users" columns are empty, and release health came back empty through the Sentry MCP (2026-11-15). Everything below is worked out from logs and metrics, one launch at a time. **Don't add identifiers or new instrumentation to get better numbers:** the owner decided against it, and it would need a privacy-policy change.

**Where:** org `regulad`, project `rosellaview`, `environment:production`. Other environments (`development`, `soak`, `soak-b`) are the owner's own testing. Production data before go-live (2026-11-12) is Amazon's review. Sentry keeps 90 days.

**The store build** is the newest `rosellaview@<n>` with no `+` suffix in `find_releases`. Versions with `+<sha>` are local builds.

**Tools:** the Sentry MCP's `search_logs`, `search_metrics`, `search_errors` and `find_releases`. Pass Sentry search syntax in `query` with explicit `fields` and `sort`. A natural-language query is sometimes used literally and returns nothing. Results stop at 100 rows, so narrow by release or period when a query hits that.

## 1. Signals

| What | How to tell |
| --- | --- |
| A launch | Each launch has its own trace ID. Count `start(RosellaView) -> true` lines. |
| A new install | `libairplay: *** A new Public Key has been created and stored in /data/rosellaview/pairing.key`, on the first launch after an install. The About screen's identity reset logs it again (`identity reset -> true` in the same trace), so count traces, not lines. A reinstall counts as a new install. |
| A returning launch | A trace with `start` but no new-key line. Nothing says which install it belongs to. With few users, timing (a relaunch minutes later) is the only hint. |
| A phone connecting | `receiver conn_init`, then `av delay for <model>: <n>ms`. Logs and metrics during a connection carry `sender.model` (e.g. `iPhone14,5`) and `sender.user_agent` (`AirPlay/<version>`). |
| Video starting | `receiver first_video_frame` and `receiver mirror_video_running`. A connection with `audio_init` but neither of those, and `video.fps` at 0, didn't show video. |
| Time connected | Playback metrics arrive every 5 s while connected: the number of `av.delay` samples × 5 s. Samples with `video.fps` above 0 give the time with video. |
| Video quality | `video.fps`, `video.fps_shown`, `video.dropped`, `video.native.errors`, `audio.concealed`. |
| Purchases | `purchase -> code <n>`: 0 is probably SUCCESSFUL (the Appstore console's sales report confirms it); 4 is a failure. `unlocked=true/false` after launch shows the entitlement. |
| Settings | `compatibility mode on/off`, `identity reset -> true`. |
| A clean exit | `receiver stopped`, from `Receiver.stop()` on unmount. If logs and metrics stop in the middle of a connection without it, and no error follows, the launch was probably killed. Investigate it. |
| Errors and crashes | `search_errors` with `environment:production`. Native crashes are sent on the next launch. Only error events carry the Fire TV model and OS version; logs and metrics don't. |

## 2. Queries

All are `search_logs` unless marked; add `period` (`14d`, `30d`).

1. **New installs:** query `environment:production message:"*new Public Key has been created*"`, fields `timestamp, message, trace, sentry.release`, sort `timestamp`.
2. **Launches:** query `environment:production message:"start(RosellaView)*"`, same fields.
3. **Everything one build did, per launch:** query `release:rosellaview@<n> environment:production !message:"libairplay:*"`, fields `timestamp, message, trace`, sort `timestamp`. Group the rows by trace. For one launch's full detail, query `trace:<id>` and keep the `libairplay:` lines.
4. **Senders:** query `has:sender.user_agent`, fields `sender.user_agent, sender.model, sentry.environment, count()`, sort `-count()`. An AirPlay version seen in production but never in `development` or `soak` hasn't been tested.
5. **Mirroring time and quality** (`search_metrics`): query `release:rosellaview@<n> metric.name:[av.delay,video.fps,video.fps_shown,video.dropped,audio.concealed]`, fields `timestamp, metric.name, value, trace, sender.model`, sort `timestamp`.
6. **Volume per build:** fields `sentry.release, sentry.environment, count()`, sort `-count()`.
7. **Errors:** `search_errors`, query `environment:production`, fields `release, count()`.

## 3. Report

1. **One row per install:** first seen (UTC), launches, and what happened (sender model and AirPlay version, whether video showed and for how long, purchases, settings changed, clean exit or not).
2. **Totals since go-live:** installs, launches, connections, time connected and time with video, purchases, errors.
3. **What to look into,** each with its trace ID: connections that never showed video, launches that ended mid-connection, errors, AirPlay versions the owner hasn't tested.
4. **One line of caveats:** every count is a minimum, because launches without network access aren't sent; reinstalls count twice; the owner's own store installs are included unless they say which ones are theirs.

Link the Sentry Explore URL that each query returns, so the owner can open it.
