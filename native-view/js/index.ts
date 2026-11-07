// JS face of xyz.regulad.rosellaview.surface: a native view that owns a GL
// surface and renders into it on its own thread (proof of concept: a
// full-range NV12 grey step chart, converted by our own shader).
//
// Copyright (C) 2026 Parker Wahle
// SPDX-License-Identifier: AGPL-3.0-or-later

// The registration helper ships with the app's react-native-kepler; it is
// resolved from the app's node_modules when Metro bundles this package.
// It is a CommonJS module (module.exports = fn), hence the interop below.
type Register = (name: string, config: object) => void;
declare const require: (id: string) => {__esModule?: boolean; default?: Register} | Register;

const NAME = 'RosellaSurface';

const mod = require('@amazon-devices/react-native-kepler/Libraries/Utilities/registerGeneratedViewConfig');
const register = (typeof mod === 'function' ? mod : mod.default) as Register;
register(NAME, {
  uiViewClassName: NAME,
  validAttributes: {},
  bubblingEventTypes: {
    topOnStatus: {
      phasedRegistrationNames: {bubbled: 'onStatus', captured: 'onStatusCapture'},
    },
  },
});

export type RosellaSurfaceStatus = {nativeEvent: {message: string}};

export type RosellaSurfaceProps = {
  style?: unknown;
  /** Progress and errors from the native view and its GL thread. */
  onStatus?: (event: RosellaSurfaceStatus) => void;
};

/** Host component: a string, as React Native expects for native views. */
export const RosellaSurface = NAME as unknown as (props: RosellaSurfaceProps) => null;
