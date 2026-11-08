/*
 * AvSync: keeps audio on the picture (lip sync).
 *
 * Audio plays in an MSE player (MirrorPlayer); video either in the native
 * pipeline (NativeVideo) or, as a fallback, in a second MSE player. Each knows
 * the sender's clock time of what it's showing or playing. Once a second the
 * two are compared; when they're out by more than the tolerance for
 * AV_BAD_CHECKS checks in a row:
 *   - native video: its capture-to-screen latency moves to match the audio,
 *     which costs nothing audible (DECISIONS.md D12);
 *   - MSE video, or beyond the native latency's bounds: the native audio
 *     pipeline drops upcoming audio (if it's late) or inserts silence (if it's
 *     early). Seeking Vega's live audio player doesn't move it.
 * Without this, a one-off hiccup in either player, or slow clock drift, stays
 * for the rest of the session (DECISIONS.md D8).
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
import {NATIVE_SLEW_MS_PER_S, NativeVideo} from './NativeVideo';
import {countEvent, log} from './Telemetry';

// Out-of-sync tolerance. People notice sound ahead of the picture at about
// 45 ms and behind it at about 125 ms (ITU-R BT.1359); ±80 ms is a fair middle.
const AV_TOLERANCE_MS = 80;
// Moving native video is cheap, so it's held closer. Its offset is judged on
// the median of the last AV_NATIVE_SAMPLES readings: single readings of the
// audio player's position jump by 100-400 ms either way.
const AV_NATIVE_TOLERANCE_MS = 50;
const AV_NATIVE_SAMPLES = 5;
const AV_BAD_CHECKS = 2;
// After a correction, wait this many checks: the shifted audio has to work
// through the player's buffer (~0.5-1 s) before the offset can be measured.
const AV_SETTLE_CHECKS = 4;
// After a native latency change, at least this many checks, plus the time it
// takes to ease in (NATIVE_SLEW_MS_PER_S).
const AV_NATIVE_SETTLE_CHECKS = 1;
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
  private samples: number[] = [];
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
    if (delta !== 0) {
      // More delay: show video sooner, or insert silence (a negative shift);
      // less: show video later, or drop audio.
      this.correct(-delta);
    }
    // Let the change play through before checking again, so the automatic
    // correction doesn't fight the viewer.
    this.settle =
      this.video instanceof NativeVideo
        ? AV_NATIVE_SETTLE_CHECKS + Math.ceil(Math.abs(delta) / NATIVE_SLEW_MS_PER_S)
        : AV_SETTLE_CHECKS;
    this.bad = 0;
    if (this.senderId) {
      AsyncStorage.setItem(DELAY_KEY + this.senderId, String(this.delayMs)).catch(() => undefined);
    }
  }

  /**
   * Brings audio `ms` earlier relative to the picture (ms < 0: later). Native
   * video takes it as that much more latency, as far as its bounds allow; the
   * rest, or all of it with MSE video, is dropped from the audio (or, for
   * ms < 0, inserted as silence). Returns the audio shift.
   */
  private correct(ms: number): number {
    let rest = ms;
    if (this.video instanceof NativeVideo) {
      const before = this.video.latencyMs;
      rest -= this.video.setLatency(before + ms) - before;
    }
    let shift = Math.max(-AV_MAX_STEP_MS, Math.min(AV_MAX_STEP_MS, Math.round(rest)));
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
      this.samples = [];
    }
    this.offsetMs = NaN;
    if (!audio) {
      return;
    }
    // Without video (audio-only), or with native video (which follows the
    // audio), the player's own latency cap keeps it live.
    const native = video instanceof NativeVideo;
    audio.latencyCap = !video || native;
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
      this.samples = [];
      return;
    }
    if (native) {
      this.samples.push(this.offsetMs);
      if (this.samples.length < AV_NATIVE_SAMPLES) {
        return;
      }
      this.samples = this.samples.slice(-AV_NATIVE_SAMPLES);
      this.offsetMs = [...this.samples].sort((x, y) => x - y)[AV_NATIVE_SAMPLES >> 1];
    }
    const tolerance = native ? AV_NATIVE_TOLERANCE_MS : AV_TOLERANCE_MS;
    if (Math.abs(this.offsetMs) <= tolerance || Math.abs(this.offsetMs) > AV_MAX_PLAUSIBLE_MS) {
      this.bad = 0;
      return;
    }
    if (!native && ++this.bad < AV_BAD_CHECKS) {
      return;
    }
    this.bad = 0;
    this.samples = [];
    this.settle = AV_SETTLE_CHECKS;
    // Late audio (negative offset): more video latency, or drop audio so it
    // plays sooner; early: less video latency, or insert silence.
    const latencyBefore = native ? (video as NativeVideo).latencyMs : 0;
    const shift = this.correct(-this.offsetMs);
    const moved = native ? (video as NativeVideo).latencyMs - latencyBefore : 0;
    if (native) {
      this.settle = AV_NATIVE_SETTLE_CHECKS + Math.ceil(Math.abs(moved) / NATIVE_SLEW_MS_PER_S);
    }
    if (shift === 0 && moved === 0) {
      return;
    }
    this.corrections++;
    countEvent('av.corrections');
    log(
      `av sync: audio ${this.offsetMs.toFixed(0)}ms off target: ` +
        (native
          ? `video latency ${moved >= 0 ? '+' : ''}${moved}ms (now ${(video as NativeVideo).latencyMs}ms), `
          : '') +
        `audio shift ${shift}ms (#${this.corrections})`,
    );
  }
}
