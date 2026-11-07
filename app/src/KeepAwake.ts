/*
 * KeepAwake: holds off Vega's screensaver (ambient mode) while the idle
 * screen waits for a sender. Vega has no keep-awake API; only active media
 * playback does it, so this loops a 2-second silent clip (assets/raw) through
 * the OS player in URL mode, which loops internally at almost no CPU cost.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {AudioPlayer} from '@amazon-devices/react-native-w3cmedia';

const SILENCE_URL = 'file:///pkg/assets/raw/silence.mp4';

type Log = (msg: string) => void;

export class KeepAwake {
  private player: AudioPlayer | null = null;
  private wanted = false;
  private starting = false;

  constructor(private readonly log: Log) {}

  /** Idempotent: call with true while idle, false once a sender connects. */
  set(on: boolean): void {
    this.wanted = on;
    if (on) {
      this.start().catch((e) => this.log(`keep-awake failed: ${String(e)}`));
    } else {
      this.stop();
    }
  }

  private async start(): Promise<void> {
    if (this.player || this.starting) {
      return;
    }
    this.starting = true;
    try {
      const ap = new AudioPlayer();
      await ap.initialize();
      if (!this.wanted) {
        ap.deinitialize();
        return;
      }
      ap.addEventListener('error', () => this.log(`keep-awake player error ${JSON.stringify(ap.error)}`));
      ap.loop = true;
      ap.src = SILENCE_URL;
      ap.play();
      this.player = ap;
      this.log('keep-awake on');
    } finally {
      this.starting = false;
    }
  }

  private stop(): void {
    const ap = this.player;
    this.player = null;
    if (ap) {
      ap.pause();
      ap.deinitialize();
      this.log('keep-awake off');
    }
  }
}
