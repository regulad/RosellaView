/*
 * About & privacy: the licence notices AGPL §5(d) asks an interactive program
 * to show, the third-party notices, the privacy disclosure, and at the end
 * the settings: the Compatibility Mode checkbox (MSE video instead of the
 * native pipeline, DECISIONS.md D12) and resetting the receiver's identity.
 *
 * One column, scrolled by the platform's own focus handling: the headings are
 * focusable (Vega's `focusable` on View), so up/down moves focus section by
 * section and the ScrollView keeps the focused one in view. The settings are
 * ordinary buttons, and the mark at the end can take focus so it's reached.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import React, {useEffect, useState} from 'react';
import {BackHandler, Image, Pressable, ScrollView, StyleSheet, Text, View} from 'react-native';
import {BUILD} from './buildInfo';

export const SOURCE_URL = 'https://github.com/regulad/RosellaView';
export const PRIVACY_URL = 'https://regulad.github.io/RosellaView/';
const LICENSE_URL = 'https://www.gnu.org/licenses/agpl-3.0.html';
// The author's mark (docs/brand/ParkerWahle.svg, white for dark backgrounds),
// rendered at twice the size it's shown.
const PERSONAL_MARK = require('./assets/personal_mark.png');
// Reset asks for a second press within this long.
const CONFIRM_MS = 4000;

type Props = {
  receiverName: string;
  onResetIdentity: () => void;
  compatibilityMode: boolean;
  onCompatibilityMode: (on: boolean) => void;
  onClose: () => void;
};

const NOTICES: [string, string, string][] = [
  ['UxPlay', 'GPL-3.0-or-later', 'F. Duncanh and contributors'],
  ['RPiPlay (crypto)', 'GPL-3.0-or-later', 'Florian Draschbacher, Jaslo Ziska'],
  ['playfair', 'GPL-3.0-or-later', 'Esteban Kubata'],
  ['libplist', 'LGPL-2.1-or-later', 'Nikias Bassen and contributors'],
  ['OpenSSL', 'Apache-2.0', 'The OpenSSL Project Authors'],
  ['FFmpeg (AAC, ALAC decoders)', 'LGPL-2.1-or-later', 'the FFmpeg developers'],
  ['Sentry SDKs', 'MIT', 'Functional Software, Inc.'],
  ['React Native, React', 'MIT', 'Meta Platforms, Inc.'],
  ['Node-API headers', 'MIT', 'Node.js contributors'],
];

/** A section heading: what the remote moves focus to, so scrolling reaches it. */
const Heading = ({children}: {children: React.ReactNode}) => (
  <View focusable style={styles.headingBlock}>
    <Text style={styles.heading}>{children}</Text>
  </View>
);

export const About = ({receiverName, onResetIdentity, compatibilityMode, onCompatibilityMode, onClose}: Props) => {
  const [confirming, setConfirming] = useState(false);
  useEffect(() => {
    if (!confirming) {
      return;
    }
    const t = setTimeout(() => setConfirming(false), CONFIRM_MS);
    return () => clearTimeout(t);
  }, [confirming]);

  useEffect(() => {
    const sub = BackHandler.addEventListener('hardwareBackPress', () => {
      onClose();
      return true;
    });
    return () => sub.remove();
  }, [onClose]);

  return (
    <View style={styles.root}>
      <ScrollView style={styles.scroll} contentContainerStyle={styles.content}>
        <View focusable hasTVPreferredFocus>
          <Text style={styles.title}>RosellaView</Text>
          <Text style={styles.body}>Version {BUILD.release.replace(/^rosellaview@/, '')}</Text>
        </View>

        <Heading>Privacy</Heading>
        <Text style={styles.small}>
          What you mirror stays on your network: your screen, video and audio go only from your device to this TV, and
          are never recorded or sent anywhere else.
        </Text>
        <Text style={styles.small}>
          To find and fix problems, RosellaView sends crash reports and diagnostics to Sentry (sentry.io): errors,
          playback and performance statistics, this TV's software version, and the model and operating system version of
          the device you mirror from (such as "iPhone18,1, iOS 27"). Nothing that identifies you or your devices is
          sent: no names, IDs or addresses. Purchases are handled by the Amazon Appstore. Privacy policy: {PRIVACY_URL}
        </Text>

        <Heading>Licence</Heading>
        <Text style={styles.small}>
          Copyright © 2026 Parker Wahle. RosellaView is free software: you can redistribute it and/or modify it under
          the terms of the GNU Affero General Public License, version 3 or (at your option) any later version. It comes
          with ABSOLUTELY NO WARRANTY. Licence: {LICENSE_URL}
        </Text>
        <Text style={styles.small}>Source code: {SOURCE_URL}</Text>

        <Heading>Includes</Heading>
        {NOTICES.map(([name, licence, holder]) => (
          <Text key={name} style={styles.notice}>
            {name} ({licence}), © {holder}
          </Text>
        ))}
        <Text style={styles.small}>
          Licence texts and source code for these are available with RosellaView's source.
        </Text>

        <Heading>Trademarks</Heading>
        <Text style={styles.small}>
          AirPlay is a trademark of Apple Inc., registered in the U.S. and other countries. RosellaView is not
          affiliated with or endorsed by Apple Inc.
        </Text>

        <Heading>Settings</Heading>
        <Pressable onPress={() => onCompatibilityMode(!compatibilityMode)} style={styles.checkRow}>
          {({focused}) => (
            <>
              <Text style={[styles.checkLabel, focused && styles.lit]}>Compatibility Mode</Text>
              <View style={[styles.box, focused && styles.boxLit]}>
                {compatibilityMode ? <Text style={[styles.check, focused && styles.lit]}>✓</Text> : null}
              </View>
            </>
          )}
        </Pressable>
        <Text style={styles.small}>
          Compatibility Mode plays mirrored video through the TV's standard video player. Try it if the picture doesn't
          appear. The brightest highlights and the darkest shadows will lose fidelity when this option is on.
        </Text>
        <Pressable
          onPress={() => {
            if (confirming) {
              setConfirming(false);
              onResetIdentity();
            } else {
              setConfirming(true);
            }
          }}
          style={styles.resetRow}>
          {({focused}) => (
            <View style={[styles.button, focused && styles.buttonLit]}>
              <Text style={[styles.buttonText, focused && styles.buttonTextLit]}>
                {confirming ? 'Press again to reset' : 'Reset Receiver Name'}
              </Text>
            </View>
          )}
        </Pressable>
        <Text style={styles.small}>
          Resetting gives this TV a new name (now "{receiverName}") and forgets the devices paired with it; select it
          again on them afterwards.
        </Text>
        <View focusable style={styles.markBlock}>
          <Image source={PERSONAL_MARK} style={styles.mark} resizeMode="contain" />
        </View>
      </ScrollView>
    </View>
  );
};

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#000', alignItems: 'center'},
  scroll: {flex: 1, width: '80%'},
  content: {paddingTop: 72, paddingBottom: 72},
  headingBlock: {marginTop: 36, marginBottom: 10},
  markBlock: {alignSelf: 'center', marginTop: 56},
  mark: {width: 177, height: 162},
  title: {color: '#fff', fontSize: 44, fontWeight: 'bold'},
  heading: {color: '#fff', fontSize: 28, fontWeight: 'bold'},
  body: {color: '#aaa', fontSize: 24, marginTop: 4},
  small: {color: '#aaa', fontSize: 22, lineHeight: 32, marginBottom: 10},
  notice: {color: '#aaa', fontSize: 22, lineHeight: 32},
  resetRow: {alignSelf: 'flex-start', paddingVertical: 8, marginTop: 16, marginBottom: 8},
  checkRow: {
    flexDirection: 'row',
    alignItems: 'center',
    alignSelf: 'flex-start',
    paddingVertical: 8,
  },
  button: {
    paddingHorizontal: 24,
    paddingVertical: 8,
    borderRadius: 26,
    borderWidth: 3,
    borderColor: '#666',
  },
  buttonLit: {backgroundColor: '#fff', borderColor: '#fff'},
  buttonText: {color: '#ddd', fontSize: 24},
  buttonTextLit: {color: '#000'},
  checkLabel: {color: '#ddd', fontSize: 26, marginRight: 18},
  box: {
    width: 34,
    height: 34,
    borderWidth: 3,
    borderColor: '#888',
    borderRadius: 4,
    alignItems: 'center',
    justifyContent: 'center',
  },
  boxLit: {borderColor: '#fff', backgroundColor: '#333'},
  check: {color: '#ddd', fontSize: 24, fontWeight: 'bold', lineHeight: 28},
  lit: {color: '#fff'},
});
