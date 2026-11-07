/*
 * CrashJournal: a fatal JS error closes a release build without a crash
 * report, so record uncaught errors and unhandled promise rejections in the
 * native journal (/scratch/rosellaview-events.log) before the default
 * handler runs.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {Receiver} from 'rosellaview-native';

type Handler = (error: unknown, isFatal?: boolean) => void;
type ErrorUtilsType = {getGlobalHandler(): Handler; setGlobalHandler(h: Handler): void};

function describe(error: unknown): string {
  if (error instanceof Error) {
    // Receiver.note keeps up to 1 KB.
    return `${error.name}: ${error.message} @ ${(error.stack ?? '').split('\n').slice(1, 4).join(' | ')}`;
  }
  return String(error);
}

export function installCrashJournal(): void {
  const g = globalThis as {
    ErrorUtils?: ErrorUtilsType;
    HermesInternal?: {enablePromiseRejectionTracker?: (opts: object) => void};
  };
  const eu = g.ErrorUtils;
  if (eu) {
    const previous = eu.getGlobalHandler();
    eu.setGlobalHandler((error, isFatal) => {
      try {
        Receiver.note(`${isFatal ? 'FATAL' : 'error'} ${describe(error)}`);
      } catch {
        // never let the journal hide the original error
      }
      previous(error, isFatal);
    });
  }
  g.HermesInternal?.enablePromiseRejectionTracker?.({
    allRejections: true,
    onUnhandled: (_id: number, error: unknown) => Receiver.note(`unhandled rejection ${describe(error)}`),
  });
}
