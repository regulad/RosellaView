/*
 * MirrorPlayer: plays the receiver's fragmented-MP4 video through the OS's
 * W3C media player (MSE). One instance per mirroring session.
 * See docs/DECISIONS.md D1 for why this path and its ~0.45 s latency.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {MediaSource, VideoPlayer} from '@amazon-devices/react-native-w3cmedia';

// The decoder reconfigures from each init segment's avcC, so a generic
// High-profile type is enough to create the SourceBuffer.
const MIME = 'video/mp4; codecs="avc1.640028"';

type Log = (msg: string) => void;

export class MirrorPlayer {
  private player: VideoPlayer | null = null;
  private source: MediaSource | null = null;
  private buffer: ReturnType<MediaSource['addSourceBuffer']> | null = null;
  private queue: ArrayBuffer[] = [];
  private busy = false;
  private playing = false;
  private haveMedia = false;
  private surface: string | null = null;
  private closed = false;
  private appended = 0;

  constructor(private readonly log: Log) {}

  async open(): Promise<void> {
    const vp = new VideoPlayer();
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
    if (this.surface) {
      vp.setSurfaceHandle(this.surface);
    }
    await opened;
    if (this.closed) {
      return;
    }
    const sb = ms.addSourceBuffer(MIME);
    sb.mode = 'sequence';
    sb.addEventListener('updateend', () => this.onUpdateEnd());
    sb.addEventListener('error', () => this.log('sourcebuffer error'));
    this.buffer = sb;
    this.log('player open');
    this.pump();
  }

  /** Called with the handle from KeplerVideoSurfaceView's onSurfaceViewCreated. */
  setSurface(handle: string | null): void {
    this.surface = handle;
    if (handle) {
      this.player?.setSurfaceHandle(handle);
    } else {
      this.player?.clearSurfaceHandle(null);
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
    if (!this.playing && this.haveMedia && this.surface) {
      this.playing = true;
      vp.play();
      this.log('play()');
    }
    // No manual remove(): the player evicts old samples itself
    // (evict_coded_frames), and removing near the read head makes it
    // declare a discontinuity and drop everything until the next keyframe.
    this.pump();
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
