/*
 * Sentry: error, log and metric reporting for RosellaView, plus delivery of
 * native crash reports.
 *
 * Built on @sentry/core alone: the React Native SDK expects iOS/Android
 * native code, which Vega doesn't have. This file supplies what a platform
 * SDK normally would: a client, a fetch transport, a Hermes stack parser,
 * global error hooks and device context. Native crashes are captured by
 * sentry-native in the module (native/src/sentry.c), whose envelopes come
 * through Receiver.onSentryEnvelope and are posted here.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {
  Client,
  applySdkMetadata,
  captureException,
  captureSession,
  createStackParser,
  createTransport,
  dedupeIntegration,
  endSession,
  eventFiltersIntegration,
  eventFromMessage,
  eventFromUnknownInput,
  flush,
  functionToStringIntegration,
  getEnvelopeEndpointWithUrlEncodedAuth,
  initAndBind,
  linkedErrorsIntegration,
  makeDsn,
  resolvedSyncPromise,
  setContext,
  setTag,
  startSession,
  withScope,
} from '@sentry/core';
import type {ClientOptions, Event, EventHint, ParameterizedString, SeverityLevel, StackFrame} from '@sentry/core';
import {Dimensions, Platform} from 'react-native';
import {Receiver} from 'rosellaview-native';
import {BUILD} from './buildInfo';

const UPLOAD_TIMEOUT_MS = 15000;

const DSN = 'https://177b93c7aaee9b05f6c07f68d6a03393@o4506961510596608.ingest.us.sentry.io/4512389338628096';

// The app's own Hermes bundle; frames from it are symbolicated with the
// source map that scripts/build.sh uploads as app:///index.bundle.map.
const APP_BUNDLES = ['index.hermes.bundle', 'index.bundle'];

/** Hermes frames: "at fn (address at /path/bundle:1:1234)", "at fn (file:12:3)", "at native". */
const HERMES_FRAME = /^\s*at (?:(.+?) \()?(?:address at )?(.+?):(\d+):(\d+)\)?\s*$/;

function hermesFrame(line: string): StackFrame | undefined {
  const m = HERMES_FRAME.exec(line);
  if (!m) {
    return undefined;
  }
  const base = m[2].split('/').pop() ?? m[2];
  const inApp = APP_BUNDLES.includes(base);
  return {
    function: m[1] || '?',
    filename: inApp ? 'app:///index.bundle' : `app:///${base}`,
    lineno: Number(m[3]),
    colno: Number(m[4]),
    in_app: inApp,
  };
}

const stackParser = createStackParser([30, hermesFrame]);

class VegaClient extends Client<ClientOptions> {
  public constructor(options: ClientOptions) {
    super(options);
  }

  // platform "javascript" makes Sentry apply the uploaded source maps.
  eventFromException(exception: unknown, hint?: EventHint): PromiseLike<Event> {
    return resolvedSyncPromise({...eventFromUnknownInput(this, stackParser, exception, hint), platform: 'javascript'});
  }

  eventFromMessage(message: ParameterizedString, level?: SeverityLevel, hint?: EventHint): PromiseLike<Event> {
    const event = eventFromMessage(stackParser, message, level, hint, this.getOptions().attachStacktrace);
    return resolvedSyncPromise({...event, platform: 'javascript'});
  }
}

function post(url: string, body: string | Uint8Array | ArrayBuffer) {
  // React Native's fetch turns a Uint8Array body into "1,2,3,..."; only an
  // ArrayBuffer goes out as bytes (minidumps are binary).
  const bytes =
    body instanceof Uint8Array ? body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength) : body;
  // Give up on a request after UPLOAD_TIMEOUT_MS: one stranded on a dead
  // connection (the network path changed) otherwise hangs until TCP times
  // out, while the transport's queue fills and new reports are dropped.
  const abort = new AbortController();
  const timer = setTimeout(() => abort.abort(), UPLOAD_TIMEOUT_MS);
  return fetch(url, {
    method: 'POST',
    body: bytes as never,
    headers: {'Content-Type': 'application/x-sentry-envelope'},
    signal: abort.signal,
  })
    .finally(() => clearTimeout(timer))
    .then((res) => ({
      statusCode: res.status,
      headers: {
        'x-sentry-rate-limits': res.headers.get('X-Sentry-Rate-Limits'),
        'retry-after': res.headers.get('Retry-After'),
      },
    }));
}

let started = false;

/** Starts reporting; call once, before anything else runs. */
export function initSentry(): void {
  if (started) {
    return;
  }
  started = true;
  const options: ClientOptions = {
    dsn: DSN,
    release: BUILD.release,
    dist: BUILD.dist,
    environment: BUILD.environment,
    // Sender model and OS are set explicitly (Telemetry.ts), to tell iPhone,
    // iPad and Mac crashes apart. No user fields: nothing personal, and no IP.
    dataCollection: {userInfo: false},
    attachStacktrace: true,
    maxBreadcrumbs: 200,
    stackParser,
    integrations: [
      eventFiltersIntegration(),
      functionToStringIntegration(),
      linkedErrorsIntegration(),
      dedupeIntegration(),
    ],
    transport: (transportOptions) =>
      createTransport(transportOptions, (request) => post(transportOptions.url, request.body)),
  };
  applySdkMetadata(options, 'vega');
  initAndBind(VegaClient, options);

  const window = Dimensions.get('window');
  setContext('device', {
    family: 'Fire TV',
    model: String((Platform.constants as Record<string, unknown>)?.Model ?? 'unknown'),
    screen_width_pixels: window.width,
    screen_height_pixels: window.height,
  });
  setContext('vega', {...(Platform.constants as Record<string, unknown>)});
  setContext('os', {name: 'Vega OS', version: String(Platform.Version ?? '')});
  setTag('build.git', BUILD.git);

  installGlobalHandlers();

  // Native crash reports (including the previous run's) are posted the same way.
  const nativeUrl = getEnvelopeEndpointWithUrlEncodedAuth(makeDsn(DSN)!);
  Receiver.onSentryEnvelope((envelope) => {
    post(nativeUrl, envelope).catch(() => undefined);
  });
  Receiver.sentryInit(DSN, BUILD.release, BUILD.dist, BUILD.environment);

  startSession();
  captureSession();
}

type Handler = (error: unknown, isFatal?: boolean) => void;
type ErrorUtilsType = {getGlobalHandler(): Handler; setGlobalHandler(h: Handler): void};

function installGlobalHandlers(): void {
  const g = globalThis as {
    ErrorUtils?: ErrorUtilsType;
    HermesInternal?: {enablePromiseRejectionTracker?: (opts: object) => void};
  };
  const eu = g.ErrorUtils;
  if (eu) {
    const previous = eu.getGlobalHandler();
    eu.setGlobalHandler((error, isFatal) => {
      withScope((scope) => {
        scope.setLevel(isFatal ? 'fatal' : 'error');
        captureException(error, {mechanism: {type: 'onerror', handled: false}});
      });
      if (!isFatal) {
        previous(error, isFatal);
        return;
      }
      // A fatal error closes the app: end the session as crashed and give the
      // report a moment to leave first.
      endSession();
      flush(2000).then(
        () => previous(error, isFatal),
        () => previous(error, isFatal),
      );
    });
  }
  g.HermesInternal?.enablePromiseRejectionTracker?.({
    allRejections: true,
    onUnhandled: (_id: number, error: unknown) =>
      captureException(error, {mechanism: {type: 'onunhandledrejection', handled: false}}),
  });
}
