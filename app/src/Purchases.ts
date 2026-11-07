/*
 * Purchases: the single "remove watermark" entitlement, via the Amazon
 * Appstore's in-app purchasing (the OS-provided keplerscript-appstore-iap-lib).
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {
  FulfillmentResult,
  PurchaseResponseCode,
  PurchaseUpdatesResponseCode,
  PurchasingService,
} from '@amazon-devices/keplerscript-appstore-iap-lib';

/** Entitlement SKU; must match the in-app item in the developer console. */
export const UNLOCK_SKU = 'rosellaview.remove_watermark';

type Log = (msg: string) => void;

/** True if the current Amazon account owns the (uncancelled) unlock. */
export async function isUnlocked(log: Log): Promise<boolean> {
  let reset = true;
  let unlocked = false;
  // Receipts arrive in pages; the first request resets to the full history.
  for (let page = 0; page < 20; page++) {
    const res = await PurchasingService.getPurchaseUpdates({reset});
    if (res.responseCode !== PurchaseUpdatesResponseCode.SUCCESSFUL) {
      log(`getPurchaseUpdates failed: code ${res.responseCode}`);
      return false;
    }
    for (const r of res.receiptList ?? []) {
      if (r.sku === UNLOCK_SKU) {
        unlocked = !r.isCancelled;
      }
    }
    if (!res.hasMore) {
      break;
    }
    reset = false;
  }
  return unlocked;
}

/** Starts the Appstore purchase flow. Resolves true once the unlock is owned. */
export async function buyUnlock(log: Log): Promise<boolean> {
  const res = await PurchasingService.purchase({sku: UNLOCK_SKU});
  log(`purchase -> code ${res.responseCode}`);
  switch (res.responseCode) {
    case PurchaseResponseCode.SUCCESSFUL:
      if (res.receipt?.receiptId) {
        // Acknowledge delivery; the watermark goes away at once.
        await PurchasingService.notifyFulfillment({
          receiptId: res.receipt.receiptId,
          fulfillmentResult: FulfillmentResult.FULFILLED,
        });
      }
      return true;
    case PurchaseResponseCode.ALREADY_PURCHASED:
      return true;
    default:
      return false;
  }
}
