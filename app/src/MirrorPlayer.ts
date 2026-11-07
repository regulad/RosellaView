/*
 * MirrorPlayer: plays one of the receiver's fragmented-MP4 streams (video or
 * audio) through the OS's W3C media player (MSE). One instance per stream.
 * Video and audio get separate players: our video timestamps are nominal
 * (fixed 1/60 s per frame, see native/src/fmp4.c), so a shared MediaSource
 * clocked by real-time audio would drop video as late.
 * See docs/DECISIONS.md D1 and D3.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {AudioPlayer, MediaSource, VideoPlayer} from '@amazon-devices/react-native-w3cmedia';

// The decoder reconfigures from each init segment's avcC, so a generic
// High-profile type is enough to create the SourceBuffer.
const VIDEO_MIME = 'video/mp4; codecs="avc1.640028"';

type Log = (msg: string) => void;

// A video player is stalled when its playhead hasn't moved for this long
// although at least STALL_MIN_FRAMES new frames arrived in the meantime (a
// still screen sends no frames, so it never looks stalled).
const STALL_MS = 2500;
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

  constructor(
    private readonly log: Log,
    private readonly stream: StreamKind = {kind: 'video'},
    /** Called once if the playhead freezes while frames keep arriving. */
    private readonly onStall?: () => void,
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
    vp.addEventListener('error', () => this.log(`player error ${JSON.stringify(vp.error)}`));
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
    sb.addEventListener('error', () => this.log('sourcebuffer error'));
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

  private pump(): void {
    const sb = this.buffer;
    if (!sb || this.busy || this.queue.length === 0) {
      return;
    }
    this.busy = true;
    try {
      sb.appendBuffer(this.queue.shift()!);
      this.appended++;
    } catch (e) {
      this.busy = false;
      this.log(`appendBuffer threw: ${String(e)}`);
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
    // No manual remove(): the player evicts old samples itself
    // (evict_coded_frames), and removing near the read head makes it
    // declare a discontinuity and drop everything until the next keyframe.
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
      this.log(`${this.stream.kind} stalled at ${t.toFixed(2)}s with ${this.appended - this.appendedAtMove} frames waiting`);
      const cb = this.onStall;
      if (this.watchdog) {
        clearInterval(this.watchdog);
        this.watchdog = null;
      }
      cb?.();
    }
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
