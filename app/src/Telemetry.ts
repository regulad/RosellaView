/*
 * Telemetry: what RosellaView reports to Sentry (see Sentry.ts): receiver
 * events as breadcrumbs and logs, the sender's details as context and tags,
 * and playback statistics as metrics.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {addBreadcrumb, captureMessage, logger, metrics, setContext, setTag} from '@sentry/core';
import type {NativeVideoState, ReceiverEvent} from 'rosellaview-native';

// Sender details, attached to every log and metric while a sender is connected.
let senderAttributes: Record<string, string> = {};

/** App-side messages: the device log (first line only, rate-limited) and Sentry logs. */
export function log(msg: string): void {
  console.log(`ROSELLAVIEW ${msg}`);
  logger.info(msg, senderAttributes);
}

/** Counts an occurrence (a Sentry counter metric with the sender attributes). */
export function countEvent(name: string): void {
  metrics.count(name, 1, {attributes: senderAttributes});
}

/** Something wrong but recoverable, worth an issue in Sentry. */
export function warn(msg: string, extra?: Record<string, unknown>): void {
  console.warn(`ROSELLAVIEW ${msg}`);
  captureMessage(msg, {level: 'warning', extra});
}

/** "iPhone18,1" → "iPhone", "MacBook8,1" → "Mac", "iPad13,4" → "iPad". */
export function senderKind(model: string): string {
  if (model.startsWith('iPhone')) {
    return 'iPhone';
  }
  if (model.startsWith('iPad')) {
    return 'iPad';
  }
  if (/^(Mac|iMac)/.test(model)) {
    return 'Mac';
  }
  if (model.startsWith('AppleTV')) {
    return 'Apple TV';
  }
  return 'other';
}


/** Every receiver event: a breadcrumb, a log, and sender/stream context where it applies. */
export function receiverEvent(e: ReceiverEvent): void {
  const {type, ...data} = e;
  const isLibLog = type === 'log';
  addBreadcrumb({
    category: isLibLog ? 'libairplay' : 'receiver',
    message: isLibLog ? String(e.msg) : type,
    data: isLibLog ? undefined : data,
    level: isLibLog && (e.level as number) <= 3 ? 'error' : 'info',
  });
  if (!isLibLog || (e.level as number) <= 5) {
    console.log(`ROSELLAVIEW ${JSON.stringify(e)}`);
  }
  logger.info(isLibLog ? `libairplay: ${String(e.msg)}` : `receiver ${type}`, {...senderAttributes, ...flatten(data)});

  switch (type) {
    case 'client_request': {
      const model = String(e.model);
      senderAttributes = {'sender.model': model, 'sender.kind': senderKind(model)};
      setContext('sender', {device_id: e.deviceid, model, name: e.name, kind: senderKind(model)});
      setTag('sender.model', model);
      setTag('sender.kind', senderKind(model));
      break;
    }
    case 'client_user_agent': {
      const ua = String(e.user_agent);
      senderAttributes['sender.user_agent'] = ua;
      setTag('sender.user_agent', ua);
      setTag('sender.airplay_version', ua.replace(/^AirPlay\//, ''));
      break;
    }
    case 'client_details': {
      const os = `${String(e.os_name)} ${String(e.os_version)}`.trim();
      senderAttributes['sender.os'] = os;
      setContext('sender_os', {name: e.os_name, version: e.os_version, build: e.os_build_version});
      setContext('sender_airplay', {source_version: e.source_version, mac_address: e.mac_address});
      setTag('sender.os', String(e.os_name));
      setTag('sender.os_version', os);
      setTag('sender.os_build', String(e.os_build_version));
      setTag('sender.airplay_version', String(e.source_version));
      break;
    }
    case 'video_init': {
      // H.264 level from the sender's SPS (byte 3): iOS's full-frame video
      // mode uses 4.0 (1080p up to ~30 fps) or 4.2 (up to ~60), so this tells
      // which encoder setting was active.
      const sps = String(e.sender_sps ?? '');
      const level = sps.length >= 8 ? parseInt(sps.slice(6, 8), 16) / 10 : NaN;
      setContext('video', {
        width: e.width,
        height: e.height,
        sender_sps: sps,
        level,
        full_range: e.full_range,
        primaries: e.primaries,
        transfer: e.transfer,
        matrix: e.matrix,
      });
      // Full range is clipped by Vega's MSE path (DECISIONS.md D9).
      senderAttributes['video.range'] = e.full_range === true ? 'full' : e.full_range === false ? 'limited' : 'unknown';
      setTag('video.range', senderAttributes['video.range']);
      if (Number.isFinite(level)) {
        senderAttributes['video.level'] = String(level);
        setTag('video.level', String(level));
      }
      break;
    }
    case 'audio_format':
      setContext('audio', {ct: e.ct, samples_per_frame: e.spf, using_screen: e.using_screen, format: e.format});
      setTag('audio.ct', String(e.ct));
      break;
    case 'conn_destroy':
      if ((e.connections as number) <= 0) {
        senderAttributes = {};
      }
      break;
    case 'error':
      captureMessage(`receiver error in ${String(e.where)}`, {level: 'error', extra: data});
      break;
  }
}

function flatten(data: Record<string, unknown>): Record<string, string | number | boolean> {
  const out: Record<string, string | number | boolean> = {};
  for (const [k, v] of Object.entries(data)) {
    out[k] = typeof v === 'number' || typeof v === 'boolean' ? v : typeof v === 'string' ? v : JSON.stringify(v);
  }
  return out;
}

export type PlayerMetrics = {
  appended: number;
  aheadMs: number;
  behindMs: number;
  queue: number;
  readyState: number;
  totalFrames: number;
  droppedFrames: number;
};

// The native pipeline's counters at the previous tick.
let prevNative: NativeVideoState | null = null;

// The video player's frame counters at the previous tick (null: new player).
let prevFrames: {t: number; total: number; dropped: number} | null = null;

let previous: {t: number; s: Record<string, number>} | null = null;
let prevCpu: {t: number; ms: number} | null = null;

/** Called every few seconds while a sender is connected. */
export function receiverStats(
  s: Record<string, unknown>,
  video: PlayerMetrics | null,
  audio: PlayerMetrics | null,
  av: {offsetMs: number; delayMs: number},
  native: NativeVideoState | null = null,
): void {
  const now = Date.now();
  const cur = s as Record<string, number>;
  const attributes = senderAttributes;
  if (previous && now > previous.t) {
    const dt = (now - previous.t) / 1000;
    const rate = (k: string) => Math.max(0, (cur[k] - previous!.s[k]) / dt);
    metrics.gauge('video.fps', rate('video_frames'), {attributes});
    metrics.gauge('video.bitrate', (rate('video_bytes') * 8) / 1000, {unit: 'kilobit', attributes});
    metrics.gauge('audio.packet_rate', rate('audio_packets'), {attributes});
    metrics.count('audio.concealed', Math.max(0, cur.audio_concealed_ms - previous.s.audio_concealed_ms), {
      unit: 'millisecond',
      attributes,
    });
  }
  previous = {t: now, s: {...cur}};
  // The sender's own frame rate, from its timestamps over this window: precise
  // enough to tell 29.97 (every other frame of a 59.94 source) from 30.00.
  if (typeof cur.sender_frames === 'number' && cur.sender_span_ms > 0) {
    metrics.gauge('video.sender_fps', ((cur.sender_frames - 1) * 1000) / cur.sender_span_ms, {attributes});
    metrics.gauge('video.interval_min', cur.sender_gap_min_ms, {unit: 'millisecond', attributes});
    metrics.gauge('video.interval_max', cur.sender_gap_max_ms, {unit: 'millisecond', attributes});
  }
  // System and process figures; each is null when the sandbox can't provide
  // it (native/src/receiver.c system_stats), and then isn't reported.
  const has = (k: string) => typeof cur[k] === 'number' && cur[k] >= 0;
  const mb = (k: string) => cur[k] / 1024;
  const gauge = (name: string, value: number, unit?: string) => metrics.gauge(name, value, {unit, attributes});
  if (has('rss_kb')) {
    gauge('process.rss', mb('rss_kb'), 'megabyte');
    gauge('process.rss_peak', mb('rss_peak_kb'), 'megabyte');
  }
  if (has('mem_available_kb')) {
    gauge('system.mem_available', mb('mem_available_kb'), 'megabyte');
  }
  if (has('mem_free_kb')) {
    gauge('system.mem_free', mb('mem_free_kb'), 'megabyte');
  }
  if (has('swap_total_kb') && has('swap_free_kb') && cur.swap_total_kb > 0) {
    gauge('system.swap_used', (cur.swap_total_kb - cur.swap_free_kb) / 1024, 'megabyte');
  }
  if (has('load1')) {
    gauge('system.load1', cur.load1);
  }
  // Our CPU time as a share of the whole device (all cores).
  if (has('proc_cpu_ms') && has('ncpu') && prevCpu && now > prevCpu.t) {
    gauge('cpu.process', (100 * (cur.proc_cpu_ms - prevCpu.ms)) / ((now - prevCpu.t) * cur.ncpu), 'percent');
  }
  prevCpu = has('proc_cpu_ms') ? {t: now, ms: cur.proc_cpu_ms} : null;
  if (has('cpu_freq_khz')) {
    gauge('cpu.freq', cur.cpu_freq_khz / 1000, 'megahertz');
  }
  if (has('cpu_freq_max_khz')) {
    gauge('cpu.freq_max', cur.cpu_freq_max_khz / 1000, 'megahertz');
  }
  if (has('threads')) {
    gauge('process.threads', cur.threads);
  }
  if (has('fds')) {
    gauge('process.fds', cur.fds);
  }
  if (has('vm_size_kb')) {
    gauge('process.vm_size', mb('vm_size_kb'), 'megabyte');
  }
  if (has('vm_locked_kb')) {
    gauge('process.vm_locked', mb('vm_locked_kb'), 'megabyte');
  }
  if (has('vm_swap_kb')) {
    gauge('process.vm_swap', mb('vm_swap_kb'), 'megabyte');
  }
  metrics.gauge('video.keyframes', cur.keyframes, {attributes});
  if (video) {
    metrics.gauge('video.buffer_ahead', video.aheadMs, {unit: 'millisecond', attributes});
    metrics.gauge('video.queue', video.queue, {attributes});
    // Already-played video still buffered; MirrorPlayer trims it to ~10-15 s.
    metrics.gauge('video.buffer_behind', video.behindMs, {unit: 'millisecond', attributes});
    // What the TV actually presents, next to what the sender sends (video.fps).
    if (video.totalFrames >= 0) {
      const p = prevFrames;
      if (p && now > p.t && video.totalFrames >= p.total) {
        const dt = (now - p.t) / 1000;
        metrics.gauge('video.fps_shown', (video.totalFrames - p.total - (video.droppedFrames - p.dropped)) / dt, {attributes});
        metrics.count('video.dropped', Math.max(0, video.droppedFrames - p.dropped), {attributes});
      }
      prevFrames = {t: now, total: video.totalFrames, dropped: video.droppedFrames};
    } else {
      prevFrames = null;
    }
  }
  // The native pipeline (hardware decode, our own rendering): its latency,
  // de-tile + upload cost, and pictures shown late or that failed to decode.
  if (native && native.attached) {
    const p = prevNative;
    metrics.gauge('video.native.latency', native.latency_ms, {unit: 'millisecond', attributes});
    metrics.gauge('video.native.upload', native.upload_ms, {unit: 'millisecond', attributes});
    metrics.gauge('video.native.queued', native.queued, {attributes});
    if (p) {
      metrics.count('video.native.late', Math.max(0, native.late - p.late), {attributes});
      metrics.count('video.native.errors', Math.max(0, native.errors - p.errors), {attributes});
      metrics.count('video.native.decoder_starts', Math.max(0, native.starts - p.starts), {attributes});
    }
    prevNative = native;
  } else {
    prevNative = null;
  }
  if (audio) {
    metrics.gauge('audio.buffer_ahead', audio.aheadMs, {unit: 'millisecond', attributes});
  }
  // Lip sync: how far audio is from its target (negative: late), and the
  // viewer's delay setting.
  if (Number.isFinite(av.offsetMs)) {
    metrics.gauge('av.offset', av.offsetMs, {unit: 'millisecond', attributes});
  }
  metrics.gauge('av.delay', av.delayMs, {unit: 'millisecond', attributes});
}

/** Call when the sender disconnects, so rates restart cleanly. */
export function resetStats(): void {
  previous = null;
  prevFrames = null;
}
