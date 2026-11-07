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
  start(name: string): boolean;
  stop(): boolean;
  isRunning(): boolean;
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
  start: (name: string): boolean => native.start(name),
  stop: (): boolean => native.stop(),
  isRunning: (): boolean => native.isRunning(),
  stats: (): Record<string, unknown> => JSON.parse(native.getStats()) as Record<string, unknown>,
};
