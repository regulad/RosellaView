// JS face of librosellaview. The module is looked up through the runtime's
// global proxy directly, so no Amazon JS is bundled with this package.

export type ReceiverEvent = {type: string; [key: string]: unknown};

/** Fragmented-MP4 pieces of the mirrored video. */
export enum SegmentKind {
  Init = 0,
  Media = 1,
}

interface NativeReceiver {
  setEventListener(listener: ((json: string) => void) | null): boolean;
  setVideoListener(listener: ((kind: number, segment: ArrayBuffer, senderMs: number) => void) | null): boolean;
  setAudioListener(listener: ((kind: number, segment: ArrayBuffer, senderMs: number) => void) | null): boolean;
  start(name: string): boolean;
  stop(): boolean;
  isRunning(): boolean;
  resetIdentity(): boolean;
  restartVideo(): boolean;
  shiftAudio(ms: number): boolean;
  requestKeyframe(): boolean;
  setSentryEnvelopeListener(listener: ((kind: number, envelope: ArrayBuffer) => void) | null): boolean;
  sentryInit(dsn: string, release: string, dist: string, environment: string): boolean;
  setWatermark(on: boolean): boolean;
  getStats(): string;
  setVideoLatency(ms: number): boolean;
  setAudioLatency(ms: number): boolean;
  nativeVideo(): string;
}

/** The native video pipeline's state (rosellaview-surface; native/include/rosella_video.h). */
export type NativeVideoState = {
  /** A native surface is decoding the mirrored video (else it goes to MSE). */
  attached: boolean;
  /** Sender clock time of what's on screen now (ms; 0 before the first frame). */
  sender_ms: number;
  latency_ms: number;
  decoded: number;
  shown: number;
  skipped: number;
  late: number;
  errors: number;
  starts: number;
  width: number;
  height: number;
  queued: number;
  upload_ms: number;
};

const g = globalThis as {__turboModuleProxy?: (name: string) => unknown};
const found = g.__turboModuleProxy ? (g.__turboModuleProxy('RosellaView') as NativeReceiver | null) : null;
if (found == null) {
  throw new Error('RosellaView native module could not be found in the native binary.');
}
const native: NativeReceiver = found;

export const Receiver = {
  onEvent(listener: ((event: ReceiverEvent) => void) | null): void {
    native.setEventListener(listener ? (json) => listener(JSON.parse(json) as ReceiverEvent) : null);
  },
  /**
   * Receives the mirrored video as fMP4: an init segment, then media segments.
   * `senderMs` is the sender's clock time of the segment's first frame (ms;
   * 0 for init segments), on the same clock as audio's, for A/V sync.
   */
  onVideo(listener: ((kind: SegmentKind, segment: ArrayBuffer, senderMs: number) => void) | null): void {
    native.setVideoListener(listener ? (kind, segment, ms) => listener(kind as SegmentKind, segment, ms) : null);
  },
  /** Receives audio as fMP4 (FLAC, or AAC-LC passthrough); a new init segment starts each stream. Like onVideo. */
  onAudio(listener: ((kind: SegmentKind, segment: ArrayBuffer, senderMs: number) => void) | null): void {
    native.setAudioListener(listener ? (kind, segment, ms) => listener(kind as SegmentKind, segment, ms) : null);
  },
  start: (name: string): boolean => native.start(name),
  stop: (): boolean => native.stop(),
  isRunning: (): boolean => native.isRunning(),
  /** A new name tag ("RosellaView XXXX") and pairing key; restarts the receiver (a new "started" event). */
  resetIdentity: (): boolean => native.resetIdentity(),
  /** After closing a stalled video player: the next keyframe, requested at once, starts a new one. */
  restartVideo: (): boolean => native.restartVideo(),
  /** A/V sync: ms > 0 drops that much upcoming audio (plays sooner), ms < 0 inserts silence (later). */
  shiftAudio: (ms: number): boolean => native.shiftAudio(Math.round(ms)),
  /** Asks the sender for an IDR frame over the AirPlay event channel; true if sent. */
  requestKeyframe: (): boolean => native.requestKeyframe(),
  /**
   * Native crash reporting (sentry-native). Set the envelope listener first:
   * reports of a crash in the previous run are sent during init.
   */
  onSentryEnvelope(listener: ((envelope: ArrayBuffer) => void) | null): void {
    native.setSentryEnvelopeListener(listener ? (_kind, envelope) => listener(envelope) : null);
  },
  sentryInit: (dsn: string, release: string, dist: string, environment: string): boolean =>
    native.sentryInit(dsn, release, dist, environment),
  /** Audio carries a spoken "Preview." every 5 s while on (the default). */
  setWatermark: (on: boolean): boolean => native.setWatermark(on),
  /** Native video: how long after capture frames are shown (ms). */
  setVideoLatency: (ms: number): boolean => native.setVideoLatency(Math.round(ms)),
  /** Native audio: how long after capture audio is heard (ms): video's latency plus the audio delay. */
  setAudioLatency: (ms: number): boolean => native.setAudioLatency(Math.round(ms)),
  nativeVideo: (): NativeVideoState => JSON.parse(native.nativeVideo()) as NativeVideoState,
  stats: (): Record<string, unknown> => JSON.parse(native.getStats()) as Record<string, unknown>,
};
