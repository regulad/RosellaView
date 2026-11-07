// JS face of librosellaview. The module is looked up through the runtime's
// global proxy directly, so no Amazon JS is bundled with this package.

export type ReceiverEvent = {type: string; [key: string]: unknown};

interface NativeReceiver {
  setEventListener(listener: ((json: string) => void) | null): boolean;
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
  start: (name: string): boolean => native.start(name),
  stop: (): boolean => native.stop(),
  isRunning: (): boolean => native.isRunning(),
  stats: (): Record<string, unknown> => JSON.parse(native.getStats()) as Record<string, unknown>,
};
