/*
 * AvSync: keeps audio on the picture (lip sync).
 *
 * Native pipeline (NativeVideo): pictures and sound both play at capture time
 * plus one fixed latency, so there's nothing to correct; only the viewer's
 * audio delay is applied (DECISIONS.md D13).
 *
 * MSE (Compatibility Mode): video and audio play in separate MSE players
 * (MirrorPlayer), each of which knows the sender's clock time of what its
 * playhead is playing. Once a second the two are compared; when audio is off
 * by more than AV_TOLERANCE_MS for AV_BAD_CHECKS checks in a row, the native
 * audio pipeline drops upcoming audio (if it's late) or inserts silence (if
 * it's early). Seeking Vega's live audio player doesn't move it. Without this,
 * a one-off hiccup in either player, or slow clock drift, stays for the rest
 * of the session (DECISIONS.md D8).
 *
 * The viewer can add an audio delay (AvDelayOverlay) for what the timestamps
 * don't capture. It's kept per sender device.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {AsyncStorage} from '@amazon-devices/react-native-kepler';
import {Receiver} from 'rosellaview-native';
import type {MirrorPlayer} from './MirrorPlayer';
import {NativeVideo} from './NativeVideo';
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
const DELAY_KEY = 'avDelayMs:'; // + the sender's AirPlay device ID
// With native video (D12) iPhones, iPads and Macs all match at 0 by eye
// (Fire TV Stick 4K Select, 2026-11-07). (MSE video wanted +300 ms for iOS.)
const DEFAULT_DELAY_MS = 0;

export class AvSync {
  private bad = 0;
  private settle = 0;
  private corrections = 0;
  private video: MirrorPlayer | NativeVideo | null = null;
  private audio: MirrorPlayer | null = null;
  private warmup = AV_WARMUP_CHECKS;
  private lastV = NaN;
  private lastA = NaN;
  /** Viewer's audio delay for the current sender (ms; positive plays audio later). */
  delayMs = 0;
  private senderId = '';
  /** Latest offset from the target (ms; negative: audio late), or NaN. */
  offsetMs = NaN;

  /** A sender connected: use its saved delay, or the default. Resolves with the delay in use. */
  async useSender(deviceId: string): Promise<number> {
    this.senderId = deviceId;
    let ms = DEFAULT_DELAY_MS;
    try {
      const saved = await AsyncStorage.getItem(DELAY_KEY + deviceId);
      if (saved !== null && Number.isFinite(Number(saved))) {
        ms = Number(saved);
      }
    } catch {
      // keep the default
    }
    if (this.senderId === deviceId) {
      // Nothing to shift: a new connection's audio starts on this target.
      this.delayMs = Math.min(AV_DELAY_MAX_MS, Math.max(AV_DELAY_MIN_MS, ms));
    }
    return this.delayMs;
  }

  /**
   * Sets and saves the viewer's delay, and applies the change to the stream
   * at once: the automatic check only acts beyond AV_TOLERANCE_MS, so steps
   * smaller than that would otherwise never be heard.
   */
  setDelay(ms: number): void {
    const next = Math.min(AV_DELAY_MAX_MS, Math.max(AV_DELAY_MIN_MS, Math.round(ms)));
    const delta = next - this.delayMs;
    this.delayMs = next;
    if (this.video instanceof NativeVideo) {
      this.video.setAudioDelay(next);
    } else if (delta !== 0) {
      // More delay: insert silence (a negative shift); less: drop audio.
      this.correct(-delta);
    }
    // Let the change play through before checking again, so the automatic
    // correction doesn't fight the viewer.
    this.settle = AV_SETTLE_CHECKS;
    this.bad = 0;
    if (this.senderId) {
      AsyncStorage.setItem(DELAY_KEY + this.senderId, String(this.delayMs)).catch(() => undefined);
    }
  }

  /**
   * Brings audio `ms` earlier relative to the picture (ms < 0: later): drops
   * that much upcoming audio (or, for ms < 0, inserts silence). Returns the
   * shift applied.
   */
  private correct(ms: number): number {
    let shift = Math.max(-AV_MAX_STEP_MS, Math.min(AV_MAX_STEP_MS, Math.round(ms)));
    if (shift > 0 && this.audio) {
      const ahead = this.audio.metrics()?.aheadMs ?? 0;
      shift = Math.min(shift, Math.max(0, ahead - AV_MIN_AUDIO_AHEAD_MS));
    }
    if (shift !== 0 && this.audio) {
      Receiver.shiftAudio(shift);
    }
    return shift;
  }

  /** Call once a second with the current players. */
  tick(video: MirrorPlayer | NativeVideo | null, audio: MirrorPlayer | null): void {
    if (video !== this.video || audio !== this.audio) {
      this.video = video;
      this.audio = audio;
      this.warmup = AV_WARMUP_CHECKS;
      this.bad = 0;
      this.lastV = this.lastA = NaN;
    }
    this.offsetMs = NaN;
    if (video instanceof NativeVideo) {
      return; // one clock for both: nothing to correct
    }
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
    const shift = this.correct(-this.offsetMs);
    if (shift === 0) {
      return;
    }
    this.corrections++;
    countEvent('av.corrections');
    log(`av sync: audio ${this.offsetMs.toFixed(0)}ms off target, shifting ${shift}ms (#${this.corrections})`);
  }
}
