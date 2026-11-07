/*
 * AvSync: keeps audio on the picture (lip sync).
 *
 * Video and audio play in separate MSE players (see MirrorPlayer), each of
 * which knows the sender's clock time of what its playhead is playing. Once a
 * second the two are compared; when audio is off by more than AV_TOLERANCE_MS
 * for AV_BAD_CHECKS checks in a row, the native pipeline drops upcoming audio
 * (if it's late) or inserts silence (if it's early). Seeking Vega's live
 * audio player doesn't move it. Without this, a one-off hiccup in either
 * player, or slow clock drift, stays for the rest of the session
 * (DECISIONS.md D8).
 *
 * The viewer can add an audio delay (AvDelayOverlay) for what the timestamps
 * can't see: the different latencies of the TV's audio and video paths.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {AsyncStorage} from '@amazon-devices/react-native-kepler';
import {Receiver} from 'rosellaview-native';
import type {MirrorPlayer} from './MirrorPlayer';
import {countEvent, log} from './Telemetry';

// Out-of-sync tolerance. People notice sound ahead of the picture at about
// 45 ms and behind it at about 125 ms (ITU-R BT.1359); ±80 ms is a fair middle.
const AV_TOLERANCE_MS = 80;
const AV_BAD_CHECKS = 2;
// After a correction, wait this many checks: the shifted audio has to work
// through the player's buffer (~0.5-1 s) before the offset can be measured.
const AV_SETTLE_CHECKS = 4;
// A new player gets this many checks to settle before it's measured.
const AV_WARMUP_CHECKS = 5;
// Bigger offsets mean something else broke (a stalled or restarted player);
// the player watchdogs deal with those.
const AV_MAX_PLAUSIBLE_MS = 3000;
// At most this much per correction, and never so much that less than
// AV_MIN_AUDIO_AHEAD_MS of audio is left buffered: dropping more starves the
// audio player, which then stalls and makes the offset grow without end.
const AV_MAX_STEP_MS = 300;
const AV_MIN_AUDIO_AHEAD_MS = 300;

export const AV_DELAY_MIN_MS = -500;
export const AV_DELAY_MAX_MS = 500;
const DELAY_KEY = 'avDelayMs';

export class AvSync {
  private bad = 0;
  private settle = 0;
  private corrections = 0;
  private video: MirrorPlayer | null = null;
  private audio: MirrorPlayer | null = null;
  private warmup = AV_WARMUP_CHECKS;
  private lastV = NaN;
  private lastA = NaN;
  /** Viewer's audio delay (ms; positive plays audio later). */
  delayMs = 0;
  /** Latest offset from the target (ms; negative: audio late), or NaN. */
  offsetMs = NaN;

  /** Loads the saved delay. */
  async load(): Promise<void> {
    try {
      const v = Number(await AsyncStorage.getItem(DELAY_KEY));
      if (Number.isFinite(v)) {
        this.delayMs = Math.min(AV_DELAY_MAX_MS, Math.max(AV_DELAY_MIN_MS, v));
      }
    } catch {
      // keep the default
    }
  }

  /** Sets and saves the viewer's delay; the next check acts on it. */
  setDelay(ms: number): void {
    this.delayMs = Math.min(AV_DELAY_MAX_MS, Math.max(AV_DELAY_MIN_MS, Math.round(ms)));
    this.settle = 0;
    this.bad = AV_BAD_CHECKS - 1;
    AsyncStorage.setItem(DELAY_KEY, String(this.delayMs)).catch(() => undefined);
  }

  /** Call once a second with the current players. */
  tick(video: MirrorPlayer | null, audio: MirrorPlayer | null): void {
    if (video !== this.video || audio !== this.audio) {
      this.video = video;
      this.audio = audio;
      this.warmup = AV_WARMUP_CHECKS;
      this.bad = 0;
      this.lastV = this.lastA = NaN;
    }
    this.offsetMs = NaN;
    if (!audio) {
      return;
    }
    // Without video (audio-only), the player's own latency cap keeps it live.
    audio.latencyCap = !video;
    if (!video) {
      return;
    }
    const v = video.playingSenderMs();
    const a = audio.playingSenderMs();
    // Both playheads must be moving: a stalled player's offset means nothing.
    const moving = v !== this.lastV && a !== this.lastA;
    this.lastV = v;
    this.lastA = a;
    if (this.warmup > 0) {
      this.warmup--;
      return;
    }
    if (!Number.isFinite(v) || !Number.isFinite(a) || !moving) {
      this.bad = 0;
      return;
    }
    // Audio should be playing the sender time video shows, minus the delay.
    this.offsetMs = a - (v - this.delayMs);
    if (this.settle > 0) {
      this.settle--;
      return;
    }
    if (Math.abs(this.offsetMs) <= AV_TOLERANCE_MS || Math.abs(this.offsetMs) > AV_MAX_PLAUSIBLE_MS) {
      this.bad = 0;
      return;
    }
    if (++this.bad < AV_BAD_CHECKS) {
      return;
    }
    this.bad = 0;
    this.settle = AV_SETTLE_CHECKS;
    // Late audio (negative offset): drop so it plays sooner; early: insert silence.
    let shift = Math.max(-AV_MAX_STEP_MS, Math.min(AV_MAX_STEP_MS, -this.offsetMs));
    if (shift > 0) {
      const ahead = audio.metrics()?.aheadMs ?? 0;
      shift = Math.min(shift, Math.max(0, ahead - AV_MIN_AUDIO_AHEAD_MS));
    }
    if (shift === 0) {
      return;
    }
    Receiver.shiftAudio(shift);
    this.corrections++;
    countEvent('av.corrections');
    log(`av sync: audio ${this.offsetMs.toFixed(0)}ms off target, shifting ${shift.toFixed(0)}ms (#${this.corrections})`);
  }
}
