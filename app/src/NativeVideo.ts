/*
 * NativeVideo: the JS face of the native pipeline (rosellaview-surface:
 * hardware video decode, our own full-range rendering, and the OS's PCM audio
 * output; DECISIONS.md D12, D13). The receiver hands it the mirrored stream
 * directly; JS only sets how long after capture pictures are shown and sound
 * is heard: one fixed latency for both, so lip sync holds by construction, plus
 * the viewer's audio delay on the sound.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {Receiver} from 'rosellaview-native';
import type {NativeVideoState} from 'rosellaview-native';
import type {PlayerMetrics} from './Telemetry';

// Capture-to-screen (and -sound) latency: enough for frames over a decent
// network to arrive in time. Bad LAN/Wi-Fi Mode (About) trades latency for
// slack on networks that deliver late (D13).
export const NATIVE_LATENCY_MS = 150;
export const BAD_NETWORK_LATENCY_MS = 700;
// Sound can't be heard before it arrives: a lower target would only drop it.
const MIN_AUDIO_LATENCY_MS = 50;

export class NativeVideo {
  private latency: number;
  private audioDelay: number;

  /** audioDelayMs: the viewer's audio delay (positive: sound later). */
  constructor(badNetwork: boolean, audioDelayMs: number) {
    this.latency = badNetwork ? BAD_NETWORK_LATENCY_MS : NATIVE_LATENCY_MS;
    this.audioDelay = audioDelayMs;
    this.apply();
  }

  get latencyMs(): number {
    return this.latency;
  }

  setBadNetwork(on: boolean): void {
    this.latency = on ? BAD_NETWORK_LATENCY_MS : NATIVE_LATENCY_MS;
    this.apply();
  }

  setAudioDelay(ms: number): void {
    this.audioDelay = ms;
    this.apply();
  }

  private apply(): void {
    Receiver.setVideoLatency(this.latency);
    Receiver.setAudioLatency(Math.max(MIN_AUDIO_LATENCY_MS, this.latency + this.audioDelay));
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
