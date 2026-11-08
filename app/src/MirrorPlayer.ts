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
import {Receiver} from 'rosellaview-native';
import {countEvent, warn} from './Telemetry';

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
const TRIM_EVERY_MS = 5000;

// Audio latency cap (audio-only mode; with video, AvSync keeps audio on the
// picture): if the audio player falls more than AUDIO_MAX_AHEAD_MS behind the
// newest audio for AUDIO_LATE_CHECKS checks in a row (1 s apart), enough
// upcoming audio is dropped natively to bring it back to
// AUDIO_TARGET_AHEAD_MS. Sequence mode never catches up by itself, and
// seeking the live audio player doesn't move it on Vega.
const AUDIO_MAX_AHEAD_MS = 1200;
const AUDIO_TARGET_AHEAD_MS = 500;
const AUDIO_LATE_CHECKS = 3;
const STALL_MIN_FRAMES = 30;

export type StreamKind = {kind: 'video'} | {kind: 'audio'; mime: string};

export class MirrorPlayer {
  private player: VideoPlayer | AudioPlayer | null = null;
  private source: MediaSource | null = null;
  private buffer: ReturnType<MediaSource['addSourceBuffer']> | null = null;
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
  private lastTrimRange = '';
  private lastUpdateEnd = 0;
  private lateChecks = 0;
  private latencyFixes = 0;
  /** The audio latency cap; AvSync turns it off while it keeps audio on video. */
  latencyCap = true;
  // Where each appended media segment landed: [media time (s), sender time (ms)],
  // oldest first, to map the playhead to the sender's clock (AvSync).
  private timeline: [number, number][] = [];
  private queue: {buf: ArrayBuffer; senderMs: number}[] = [];

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
    } else if (!this.isVideo) {
      this.watchdog = setInterval(() => this.checkLatency(), 1000);
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

  /** `senderMs`: the sender's clock time of the segment's first sample (0 if none). */
  push(segment: ArrayBuffer, isMedia: boolean, senderMs = 0): void {
    if (this.closed) {
      return;
    }
    this.queue.push({buf: segment, senderMs: isMedia ? senderMs : 0});
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
    if (!sb || !vp || !this.isVideo || (!force && now - this.lastTrim < TRIM_EVERY_MS)) {
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
      this.lastTrimRange = `${b.start(0).toFixed(1)}-${end.toFixed(1)}`;
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
      // In sequence mode a segment lands at the end of what's buffered.
      const vp = this.player;
      const b = vp?.buffered;
      const start = b && b.length ? b.end(b.length - 1) : 0;
      sb.appendBuffer(next.buf);
      this.appended++;
      if (next.senderMs > 0) {
        this.timeline.push([start, next.senderMs]);
        // Keep about the last 60 s.
        while (this.timeline.length > 1 && this.timeline[0][0] < start - 60) {
          this.timeline.shift();
        }
      }
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
    this.lastUpdateEnd = Date.now();
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

  metrics(): {
    appended: number;
    aheadMs: number;
    behindMs: number;
    queue: number;
    readyState: number;
    totalFrames: number;
    droppedFrames: number;
  } | null {
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
      ...this.playbackQuality(),
    };
  }

  /** The player's decoded and dropped frame counts (video only; -1 if unavailable). */
  private playbackQuality(): {totalFrames: number; droppedFrames: number} {
    // Vega's object names its fields with a trailing underscore.
    type Quality = Record<string, number | undefined>;
    const vp = this.player as unknown as {getVideoPlaybackQuality?: () => Quality} | null;
    const q = this.isVideo ? vp?.getVideoPlaybackQuality?.() : undefined;
    return {
      totalFrames: q?.totalVideoFrames ?? q?.totalVideoFrames_ ?? -1,
      droppedFrames: q?.droppedVideoFrames ?? q?.droppedVideoFrames_ ?? -1,
    };
  }

  /** Keeps audio latency near AUDIO_TARGET_AHEAD_MS (see AUDIO_MAX_AHEAD_MS). */
  private checkLatency(): void {
    const vp = this.player;
    if (!vp || !this.playing || this.closed) {
      return;
    }
    const b = vp.buffered;
    if (!b || !b.length) {
      return;
    }
    const end = b.end(b.length - 1);
    const aheadMs = (end - vp.currentTime) * 1000;
    if (this.lateChecks < 0) {
      this.lateChecks++;
      return;
    }
    this.lateChecks = this.latencyCap && aheadMs > AUDIO_MAX_AHEAD_MS ? this.lateChecks + 1 : 0;
    if (this.lateChecks >= AUDIO_LATE_CHECKS) {
      this.latencyFixes++;
      Receiver.shiftAudio(aheadMs - AUDIO_TARGET_AHEAD_MS);
      // Give the shortened audio time to reach the playhead.
      this.lateChecks = -AUDIO_LATE_CHECKS;
      this.log(
        `${this.stream.kind} latency ${aheadMs.toFixed(0)}ms: dropping ${(aheadMs - AUDIO_TARGET_AHEAD_MS).toFixed(0)}ms (#${this.latencyFixes})`,
      );
      countEvent(`${this.stream.kind}.latency_corrections`);
    }
  }

  /** The sender's clock time (ms) of what the playhead is playing, or NaN. */
  playingSenderMs(): number {
    const vp = this.player;
    if (!vp || !this.playing || this.closed) {
      return NaN;
    }
    const t = vp.currentTime;
    const tl = this.timeline;
    let i = tl.length - 1;
    while (i >= 0 && tl[i][0] > t) {
      i--;
    }
    return i < 0 ? NaN : tl[i][1] + (t - tl[i][0]) * 1000;
  }

  stats(): string {
    const vp = this.player;
    if (!vp) {
      return 'no player';
    }
    const b = vp.buffered;
    const end = b && b.length ? b.end(b.length - 1) : -1;
    const now = Date.now();
    return (
      `appended=${this.appended} cur=${vp.currentTime.toFixed(2)} ahead=${((end - vp.currentTime) * 1000).toFixed(0)}ms ` +
      `queue=${this.queue.length} rs=${vp.readyState} ranges=${b ? b.length : 0} busy=${this.busy} ` +
      `lastUpdateEnd=${this.lastUpdateEnd ? now - this.lastUpdateEnd : -1}ms ` +
      `lastTrim=${this.lastTrim ? now - this.lastTrim : -1}ms(${this.lastTrimRange}) trims=${this.trims}`
    );
  }
}
