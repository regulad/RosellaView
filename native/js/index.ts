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
  setVideoListener(listener: ((kind: number, segment: ArrayBuffer) => void) | null): boolean;
  setAudioListener(listener: ((kind: number, segment: ArrayBuffer) => void) | null): boolean;
  start(name: string): boolean;
  stop(): boolean;
  isRunning(): boolean;
  resyncVideo(): boolean;
  canResyncVideo(): boolean;
  note(msg: string): boolean;
  setWatermark(on: boolean): boolean;
  getStats(): string;
}

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
  /** Receives the mirrored video as fMP4: an init segment, then media segments. */
  onVideo(listener: ((kind: SegmentKind, segment: ArrayBuffer) => void) | null): void {
    native.setVideoListener(listener ? (kind, segment) => listener(kind as SegmentKind, segment) : null);
  },
  /** Receives audio as fMP4 (FLAC, or AAC-LC passthrough); a new init segment starts each stream. */
  onAudio(listener: ((kind: SegmentKind, segment: ArrayBuffer) => void) | null): void {
    native.setAudioListener(listener ? (kind, segment) => listener(kind as SegmentKind, segment) : null);
  },
  start: (name: string): boolean => native.start(name),
  stop: (): boolean => native.stop(),
  isRunning: (): boolean => native.isRunning(),
  /** After rebuilding a stalled video player: replays the current GOP to it. */
  resyncVideo: (): boolean => native.resyncVideo(),
  /** Whether resyncVideo() has a GOP to replay (iOS may send only one keyframe per session). */
  canResyncVideo: (): boolean => native.canResyncVideo(),
  /** Appends a line to the development journal (/scratch/rosellaview-events.log). */
  note: (msg: string): boolean => native.note(msg),
  /** Audio carries a spoken "Preview." every 5 s while on (the default). */
  setWatermark: (on: boolean): boolean => native.setWatermark(on),
  stats: (): Record<string, unknown> => JSON.parse(native.getStats()) as Record<string, unknown>,
};
