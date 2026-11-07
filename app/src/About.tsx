/*
 * About & privacy: the licence notices AGPL §5(d) asks an interactive program
 * to show, the third-party notices, the privacy disclosure, and the
 * Compatibility Mode switch (MSE video instead of the native pipeline,
 * DECISIONS.md D12). Laid out to fit one screen, so it needs no scrolling.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import React, {useEffect} from 'react';
import {BackHandler, Pressable, StyleSheet, Text, View} from 'react-native';
import {BUILD} from './buildInfo';

export const SOURCE_URL = 'https://github.com/regulad/RosellaView';
const LICENSE_URL = 'https://www.gnu.org/licenses/agpl-3.0.html';

type Props = {
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

export const About = ({compatibilityMode, onCompatibilityMode, onClose}: Props) => {
  useEffect(() => {
    const sub = BackHandler.addEventListener('hardwareBackPress', () => {
      onClose();
      return true;
    });
    return () => sub.remove();
  }, [onClose]);

  return (
    <View style={styles.root}>
      <View style={styles.column}>
        <Text style={styles.title}>RosellaView</Text>
        <Text style={styles.body}>Version {BUILD.release.replace(/^rosellaview@/, '')}</Text>

        <Pressable
          hasTVPreferredFocus
          onPress={() => onCompatibilityMode(!compatibilityMode)}
          style={({focused}) => [styles.toggle, focused && styles.toggleFocused]}>
          {({focused}) => (
            <Text style={[styles.toggleText, focused && styles.toggleTextFocused]}>
              Compatibility Mode: {compatibilityMode ? 'On' : 'Off'}
            </Text>
          )}
        </Pressable>
        <Text style={styles.small}>
          Plays mirrored video through the TV's standard video player. Try it if the picture doesn't appear. With it
          on, the darkest and lightest shades from iPhones and iPads can merge.
        </Text>

        <Text style={styles.heading}>Privacy</Text>
        <Text style={styles.small}>
          What you mirror stays on your network: your screen, video and audio go only from your device to this TV,
          and are never recorded or sent anywhere else.
        </Text>
        <Text style={styles.small}>
          To find and fix problems, RosellaView sends crash reports and diagnostics to Sentry (sentry.io): errors,
          playback and performance statistics, this TV's software version, and the name, model, operating system
          version, AirPlay ID and network address (MAC) of the device you mirror from. Purchases are handled by the
          Amazon Appstore.
        </Text>
        <Text style={styles.hint}>Press Back to return.</Text>
      </View>

      <View style={styles.column}>
        <Text style={styles.heading}>Licence</Text>
        <Text style={styles.small}>
          Copyright © 2026 Parker Wahle. RosellaView is free software: you can redistribute it and/or modify it under
          the terms of the GNU Affero General Public License, version 3 or (at your option) any later version. It
          comes with ABSOLUTELY NO WARRANTY. Licence: {LICENSE_URL}
        </Text>
        <Text style={styles.small}>Source code: {SOURCE_URL}</Text>

        <Text style={styles.heading}>Includes</Text>
        {NOTICES.map(([name, licence, holder]) => (
          <Text key={name} style={styles.notice}>
            {name} ({licence}), © {holder}
          </Text>
        ))}
        <Text style={styles.small}>Licence texts and source code for these are available with RosellaView's source.</Text>

        <Text style={styles.heading}>Trademarks</Text>
        <Text style={styles.small}>
          AirPlay is a trademark of Apple Inc., registered in the U.S. and other countries. RosellaView is not
          affiliated with or endorsed by Apple Inc.
        </Text>
      </View>
    </View>
  );
};

const styles = StyleSheet.create({
  root: {flex: 1, flexDirection: 'row', backgroundColor: '#000', paddingHorizontal: 96, paddingVertical: 64},
  column: {flex: 1, marginHorizontal: 32},
  title: {color: '#fff', fontSize: 44, fontWeight: 'bold'},
  heading: {color: '#fff', fontSize: 26, fontWeight: 'bold', marginTop: 28, marginBottom: 8},
  body: {color: '#aaa', fontSize: 22, marginTop: 4},
  small: {color: '#aaa', fontSize: 19, lineHeight: 27, marginBottom: 8},
  notice: {color: '#aaa', fontSize: 19, lineHeight: 26},
  hint: {color: '#666', fontSize: 19, marginTop: 24},
  toggle: {
    alignSelf: 'flex-start',
    marginTop: 28,
    marginBottom: 10,
    paddingHorizontal: 28,
    paddingVertical: 10,
    borderRadius: 30,
    borderWidth: 3,
    borderColor: '#666',
  },
  toggleFocused: {backgroundColor: '#fff', borderColor: '#fff'},
  toggleText: {color: '#ddd', fontSize: 24},
  toggleTextFocused: {color: '#000'},
});
