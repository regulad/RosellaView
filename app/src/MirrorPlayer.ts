/*
 * MirrorPlayer: plays one of the receiver's fragmented-MP4 streams (video or
 * audio) through the OS's W3C media player (MSE). One instance per stream.
 * Video and audio get separate players: video pauses longer than 190 ms (a
 * still screen) are cut short in the timestamps (see native/src/fmp4.c), so a
 * shared MediaSource clocked by real-time audio would drop video as late.
 * See docs/DECISIONS.md D1 and D3.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {AudioPlayer, MediaSource, VideoPlayer} from '@amazon-devices/react-native-w3cmedia';
import {warn} from './Telemetry';

// The decoder reconfigures from each init segment's avcC, so a generic
// High-profile type is enough to create the SourceBuffer.
const VIDEO_MIME = 'video/mp4; codecs="avc1.640028"';

type Log = (msg: string) => void;

// A video player is stalled when its playhead hasn't moved for this long
// although at least STALL_MIN_FRAMES new frames arrived in the meantime (a
// still screen sends no frames, so it never looks stalled).
const STALL_MS = 2500;

// Played video further back than BACK_BUFFER_S is removed every TRIM_EVERY_MS,
// as live MSE players do (hls.js's backBufferLength): Vega's player doesn't
// evict played video itself, so the SourceBuffer would fill (100 MB, about
// 2 min of a Mac's 8 Mbit/s) and playback freeze. A remove stops at the next
// sync sample, which fmp4.c flags at least once a second (DECISIONS.md D6).
const BACK_BUFFER_S = 10;
// A/B test (DECISIONS.md D6 "To revisit"): false lets the player manage the
// buffer alone, now that senders send keyframes.
const TRIM_ENABLED = true;
const TRIM_EVERY_MS = 5000;
const STALL_MIN_FRAMES = 30;

export type StreamKind = {kind: 'video'} | {kind: 'audio'; mime: string};

export class MirrorPlayer {
  private player: VideoPlayer | AudioPlayer | null = null;
  private source: MediaSource | null = null;
  private buffer: ReturnType<MediaSource['addSourceBuffer']> | null = null;
  private queue: ArrayBuffer[] = [];
  private busy = false;
  private playing = false;
  private haveMedia = false;
  private surface: string | null = null;
  private closed = false;
  private appended = 0;
  private watchdog: ReturnType<typeof setInterval> | null = null;
  private lastTime = -1;
  private lastMove = 0;
  private appendedAtMove = 0;
  private lastTrim = 0;
  private trims = 0;

  constructor(
    private readonly log: Log,
    private readonly stream: StreamKind = {kind: 'video'},
    /** Called every STALL_MS while the playhead is frozen and frames keep arriving. */
    private readonly onStall?: (p: MirrorPlayer) => void,
  ) {}

  private get isVideo(): boolean {
    return this.stream.kind === 'video';
  }

  async open(): Promise<void> {
    const vp = this.isVideo ? new VideoPlayer() : new AudioPlayer();
    await vp.initialize();
    if (this.closed) {
      vp.deinitialize();
      return;
    }
    this.player = vp;
    vp.addEventListener('error', () => warn(`${this.stream.kind} player error`, {error: JSON.stringify(vp.error)}));
    const ms = new MediaSource();
    this.source = ms;
    const opened = new Promise<void>((resolve) => ms.addEventListener('sourceopen', () => resolve()));
    vp.src = URL.createObjectURL(ms);
    if (this.surface && vp instanceof VideoPlayer) {
      vp.setSurfaceHandle(this.surface);
    }
    await opened;
    if (this.closed) {
      return;
    }
    const sb = ms.addSourceBuffer(this.stream.kind === 'video' ? VIDEO_MIME : this.stream.mime);
    sb.mode = 'sequence';
    sb.addEventListener('updateend', () => this.onUpdateEnd());
    sb.addEventListener('error', () => warn(`${this.stream.kind} sourcebuffer error`));
    this.buffer = sb;
    this.log(`${this.stream.kind} player open`);
    if (this.onStall) {
      this.watchdog = setInterval(() => this.checkStall(), 1000);
    }
    this.pump();
  }

  /** Called with the handle from KeplerVideoSurfaceView's onSurfaceViewCreated. */
  setSurface(handle: string | null): void {
    this.surface = handle;
    const vp = this.player;
    if (!(vp instanceof VideoPlayer)) {
      return;
    }
    if (handle) {
      vp.setSurfaceHandle(handle);
    } else {
      vp.clearSurfaceHandle(null);
    }
  }

  /** Linear volume, 0..1. */
  setVolume(volume: number): void {
    if (this.player) {
      this.player.volume = Math.max(0, Math.min(1, volume));
    }
  }

  push(segment: ArrayBuffer, isMedia: boolean): void {
    if (this.closed) {
      return;
    }
    this.queue.push(segment);
    if (isMedia) {
      this.haveMedia = true;
    }
    this.pump();
  }

  close(): void {
    this.closed = true;
    if (this.watchdog) {
      clearInterval(this.watchdog);
      this.watchdog = null;
    }
    this.queue = [];
    const vp = this.player;
    this.player = null;
    this.buffer = null;
    this.source = null;
    if (vp) {
      vp.pause();
      vp.deinitialize();
    }
  }

  /** Starts removing played video if it's time (or `force`); true if a remove is under way. */
  private trim(force: boolean): boolean {
    const sb = this.buffer;
    const vp = this.player;
    const now = Date.now();
    if (!TRIM_ENABLED || !sb || !vp || !this.isVideo || (!force && now - this.lastTrim < TRIM_EVERY_MS)) {
      return false;
    }
    this.lastTrim = now;
    const b = vp.buffered;
    const end = vp.currentTime - BACK_BUFFER_S;
    if (!b || !b.length || b.start(0) >= end) {
      return false;
    }
    try {
      sb.remove(b.start(0), end);
      this.trims++;
      return true;
    } catch (e) {
      warn(`${this.stream.kind} remove threw`, {error: String(e)});
      return false;
    }
  }

  private pump(): void {
    const sb = this.buffer;
    if (!sb || this.busy) {
      return;
    }
    this.busy = true;
    if (this.trim(false)) {
      return; // updateend pumps again
    }
    if (this.queue.length === 0) {
      this.busy = false;
      return;
    }
    const next = this.queue.shift()!;
    try {
      sb.appendBuffer(next);
      this.appended++;
    } catch (e) {
      if (String(e).includes('QuotaExceeded')) {
        // Full anyway: keep the segment, free space, retry on updateend.
        this.queue.unshift(next);
        if (this.trim(true)) {
          return;
        }
      }
      this.busy = false;
      warn(`${this.stream.kind} appendBuffer threw`, {error: String(e), trims: this.trims});
    }
  }

  private onUpdateEnd(): void {
    this.busy = false;
    const vp = this.player;
    if (!vp || this.closed) {
      return;
    }
    // Start playback as soon as media is buffered; waiting longer only adds latency.
    if (!this.playing && this.haveMedia && (this.surface || !this.isVideo)) {
      this.playing = true;
      vp.play();
      this.log(`${this.stream.kind} play()`);
    }
    // Played video is removed in trim(), well behind the playhead: removing
    // near the read head makes the player declare a discontinuity and drop
    // everything until the next sync sample.
    this.pump();
  }

  private checkStall(): void {
    const vp = this.player;
    if (!vp || !this.playing || this.closed) {
      return;
    }
    const now = Date.now();
    const t = vp.currentTime;
    if (t !== this.lastTime) {
      this.lastTime = t;
      this.lastMove = now;
      this.appendedAtMove = this.appended;
      return;
    }
    if (now - this.lastMove >= STALL_MS && this.appended - this.appendedAtMove >= STALL_MIN_FRAMES) {
      this.log(`${this.stream.kind} stalled: ${this.stats()}`);
      this.lastMove = now; // at most one call per STALL_MS
      this.appendedAtMove = this.appended;
      this.onStall?.(this);
    }
  }

  /** Nudges a stalled player without dropping its decoder state. */
  kick(): void {
    const vp = this.player;
    if (vp && !this.closed) {
      vp.play();
    }
  }

  metrics(): {appended: number; aheadMs: number; behindMs: number; queue: number; readyState: number} | null {
    const vp = this.player;
    if (!vp) {
      return null;
    }
    const b = vp.buffered;
    const end = b && b.length ? b.end(b.length - 1) : vp.currentTime;
    const start = b && b.length ? b.start(0) : vp.currentTime;
    return {
      appended: this.appended,
      aheadMs: Math.round((end - vp.currentTime) * 1000),
      behindMs: Math.round((vp.currentTime - start) * 1000),
      queue: this.queue.length,
      readyState: vp.readyState,
    };
  }

  stats(): string {
    const vp = this.player;
    if (!vp) {
      return 'no player';
    }
    const b = vp.buffered;
    const end = b && b.length ? b.end(b.length - 1) : -1;
    return `appended=${this.appended} cur=${vp.currentTime.toFixed(2)} ahead=${((end - vp.currentTime) * 1000).toFixed(0)}ms queue=${this.queue.length} rs=${vp.readyState}`;
  }
}
