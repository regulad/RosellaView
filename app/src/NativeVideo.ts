/*
 * NativeVideo: the JS face of the native video pipeline (rosellaview-surface:
 * hardware decode and our own full-range rendering, DECISIONS.md D12). The
 * receiver hands it the mirrored H.264 directly; JS only sets how long after
 * capture frames are shown, which AvSync moves to match the audio player.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {Receiver} from 'rosellaview-native';
import type {NativeVideoState} from 'rosellaview-native';
import type {PlayerMetrics} from './Telemetry';

// Capture-to-screen latency at the start of a session, before A/V sync has
// measured the audio player; and its bounds. Below the minimum, frames over
// Wi-Fi arrive after they're due.
export const NATIVE_LATENCY_START_MS = 350;
export const NATIVE_LATENCY_MIN_MS = 80;
export const NATIVE_LATENCY_MAX_MS = 1500;
// The native side eases into a new latency at this rate (kSlewPerSecond in
// native-view/src/RosellaSurface.cpp): a 10% speed change, not a jump.
export const NATIVE_SLEW_MS_PER_S = 100;

export class NativeVideo {
  private latency = NATIVE_LATENCY_START_MS;

  constructor() {
    Receiver.setVideoLatency(this.latency);
  }

  get latencyMs(): number {
    return this.latency;
  }

  /** Sets the latency within its bounds; returns what was applied. */
  setLatency(ms: number): number {
    this.latency = Math.round(Math.min(NATIVE_LATENCY_MAX_MS, Math.max(NATIVE_LATENCY_MIN_MS, ms)));
    Receiver.setVideoLatency(this.latency);
    return this.latency;
  }

  state(): NativeVideoState {
    return Receiver.nativeVideo();
  }

  /** The sender's clock time (ms) of the picture on screen, or NaN. */
  playingSenderMs(): number {
    const s = this.state();
    return s.attached && s.sender_ms > 0 ? s.sender_ms : NaN;
  }

  /** In MirrorPlayer's shape, for the shared video gauges. */
  metrics(): PlayerMetrics {
    const s = this.state();
    return {
      appended: s.decoded,
      aheadMs: this.latency,
      behindMs: 0,
      queue: s.queued,
      readyState: s.attached ? 4 : 0,
      totalFrames: s.shown + s.skipped,
      droppedFrames: s.skipped,
    };
  }
}
