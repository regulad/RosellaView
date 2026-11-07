/*
 * Telemetry: what RosellaView reports to Sentry (see Sentry.ts): receiver
 * events as breadcrumbs and logs, the sender's details as context and tags,
 * and playback statistics as metrics.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {addBreadcrumb, captureMessage, logger, metrics, setContext, setTag} from '@sentry/core';
import type {ReceiverEvent} from 'rosellaview-native';

// Sender details, attached to every log and metric while a sender is connected.
let senderAttributes: Record<string, string> = {};

/** App-side messages: the device log (first line only, rate-limited) and Sentry logs. */
export function log(msg: string): void {
  console.log(`ROSELLAVIEW ${msg}`);
  logger.info(msg, senderAttributes);
}

/** Something wrong but recoverable, worth an issue in Sentry. */
export function warn(msg: string, extra?: Record<string, unknown>): void {
  console.warn(`ROSELLAVIEW ${msg}`);
  captureMessage(msg, {level: 'warning', extra});
}

/** "iPhone18,1" → "iPhone", "MacBook8,1" → "Mac", "iPad13,4" → "iPad". */
function senderKind(model: string): string {
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
    case 'video_init':
      setContext('video', {width: e.width, height: e.height, sender_sps: e.sender_sps});
      break;
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

export type PlayerMetrics = {appended: number; aheadMs: number; behindMs: number; queue: number; readyState: number};

let previous: {t: number; s: Record<string, number>} | null = null;
let prevCpu: {total: number; busy: number; proc: number} | null = null;

/** Called every few seconds while a sender is connected. */
export function receiverStats(s: Record<string, unknown>, video: PlayerMetrics | null, audio: PlayerMetrics | null): void {
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
  // Memory: the app's own, and the device's (the media buffers may live in
  // OS services rather than in our process).
  if (cur.rss_kb > 0) {
    metrics.gauge('process.rss', cur.rss_kb / 1024, {unit: 'megabyte', attributes});
    metrics.gauge('process.rss_peak', cur.rss_peak_kb / 1024, {unit: 'megabyte', attributes});
  }
  if (cur.mem_available_kb > 0) {
    metrics.gauge('system.mem_available', cur.mem_available_kb / 1024, {unit: 'megabyte', attributes});
  }
  if (cur.swap_total_kb > 0) {
    metrics.gauge('system.swap_used', (cur.swap_total_kb - cur.swap_free_kb) / 1024, {unit: 'megabyte', attributes});
  }
  // CPU: device-wide busy share, and our process's share of the whole device.
  if (prevCpu && cur.cpu_total_ticks > prevCpu.total) {
    const total = cur.cpu_total_ticks - prevCpu.total; // summed over all cores
    metrics.gauge('cpu.system', (100 * (cur.cpu_busy_ticks - prevCpu.busy)) / total, {unit: 'percent', attributes});
    metrics.gauge('cpu.process', (100 * (cur.proc_cpu_ticks - prevCpu.proc)) / total, {unit: 'percent', attributes});
  }
  prevCpu = {total: cur.cpu_total_ticks, busy: cur.cpu_busy_ticks, proc: cur.proc_cpu_ticks};
  metrics.gauge('cpu.freq', cur.cpu_freq_khz / 1000, {unit: 'megahertz', attributes});
  metrics.gauge('cpu.freq_max', cur.cpu_freq_max_khz / 1000, {unit: 'megahertz', attributes});
  metrics.gauge('system.load1', cur.load1, {attributes});
  // Pressure stall information: % of the last 10 s some task waited on memory / CPU.
  metrics.gauge('system.pressure.memory', cur.psi_mem_some10, {unit: 'percent', attributes});
  metrics.gauge('system.pressure.cpu', cur.psi_cpu_some10, {unit: 'percent', attributes});
  metrics.gauge('process.threads', cur.threads, {attributes});
  metrics.gauge('process.fds', cur.fds, {attributes});
  metrics.gauge('video.keyframes', cur.keyframes, {attributes});
  if (video) {
    metrics.gauge('video.buffer_ahead', video.aheadMs, {unit: 'millisecond', attributes});
    metrics.gauge('video.queue', video.queue, {attributes});
    // Already-played video still buffered; MirrorPlayer trims it to ~10-15 s.
    metrics.gauge('video.buffer_behind', video.behindMs, {unit: 'millisecond', attributes});
  }
  if (audio) {
    metrics.gauge('audio.buffer_ahead', audio.aheadMs, {unit: 'millisecond', attributes});
  }
}

/** Call when the sender disconnects, so rates restart cleanly. */
export function resetStats(): void {
  previous = null;
}
