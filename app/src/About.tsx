/*
 * About & privacy: the licence notices AGPL §5(d) asks an interactive program
 * to show, the third-party notices, the privacy disclosure, and at the end
 * the settings: the Compatibility Mode checkbox (MSE video instead of the
 * native pipeline, DECISIONS.md D12) and resetting the receiver's identity.
 *
 * One column, scrolled with up/down on the remote: text isn't focusable, so
 * the focus engine wouldn't scroll it. The settings at the end answer select
 * only once they're scrolled into view; the reset button can't take focus
 * before then, and up from it moves focus to the checkbox, not the page.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {useTVEventHandler} from '@amazon-devices/react-native-kepler';
import type {HWEvent} from '@amazon-devices/react-native-kepler';
import React, {useEffect, useRef, useState} from 'react';
import {BackHandler, Pressable, ScrollView, StyleSheet, Text, View} from 'react-native';
import {BUILD} from './buildInfo';

export const SOURCE_URL = 'https://github.com/regulad/RosellaView';
export const PRIVACY_URL = 'https://regulad.github.io/RosellaView/';
const LICENSE_URL = 'https://www.gnu.org/licenses/agpl-3.0.html';
const SCROLL_STEP = 240;
// Reset asks for a second press within this long.
const CONFIRM_MS = 4000;
const KEY_UP = 1;

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

export const About = ({receiverName, onResetIdentity, compatibilityMode, onCompatibilityMode, onClose}: Props) => {
  const [confirming, setConfirming] = useState(false);
  useEffect(() => {
    if (!confirming) {
      return;
    }
    const t = setTimeout(() => setConfirming(false), CONFIRM_MS);
    return () => clearTimeout(t);
  }, [confirming]);
  const scroll = useRef<ScrollView | null>(null);
  const offset = useRef(0);
  const [size, setSize] = useState({view: 0, content: 0});
  const maxOffset = Math.max(0, size.content - size.view);
  const [atEnd, setAtEnd] = useState(false);
  const latest = useRef({maxOffset});
  latest.current = {maxOffset};

  useEffect(() => {
    const sub = BackHandler.addEventListener('hardwareBackPress', () => {
      onClose();
      return true;
    });
    return () => sub.remove();
  }, [onClose]);

  useEffect(() => {
    setAtEnd(size.content > 0 && offset.current >= maxOffset - 1);
  }, [size, maxOffset]);

  const resetFocused = useRef(false);

  useTVEventHandler((e: HWEvent) => {
    if (e.eventKeyAction === KEY_UP || (e.eventType !== 'up' && e.eventType !== 'down')) {
      return;
    }
    if (e.eventType === 'up' && resetFocused.current) {
      return; // focus moves up to the checkbox
    }
    const max = latest.current.maxOffset;
    const y = Math.min(max, Math.max(0, offset.current + (e.eventType === 'down' ? SCROLL_STEP : -SCROLL_STEP)));
    offset.current = y;
    setAtEnd(y >= max - 1);
    scroll.current?.scrollTo({y, animated: true});
  });

  return (
    <View style={styles.root}>
      <ScrollView
        ref={scroll}
        style={styles.scroll}
        contentContainerStyle={styles.content}
        scrollEnabled={false}
        onLayout={(e) => setSize((s) => ({...s, view: e.nativeEvent.layout.height}))}
        onContentSizeChange={(_w, h) => setSize((s) => ({...s, content: h}))}
        // In case the focus engine scrolls to the checkbox itself.
        scrollEventThrottle={100}
        onScroll={(e) => {
          offset.current = e.nativeEvent.contentOffset.y;
          setAtEnd(offset.current >= latest.current.maxOffset - 1);
        }}>
        <Text style={styles.title}>RosellaView</Text>
        <Text style={styles.body}>Version {BUILD.release.replace(/^rosellaview@/, '')}</Text>

        <Text style={styles.heading}>Privacy</Text>
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

        <Text style={styles.heading}>Licence</Text>
        <Text style={styles.small}>
          Copyright © 2026 Parker Wahle. RosellaView is free software: you can redistribute it and/or modify it under
          the terms of the GNU Affero General Public License, version 3 or (at your option) any later version. It comes
          with ABSOLUTELY NO WARRANTY. Licence: {LICENSE_URL}
        </Text>
        <Text style={styles.small}>Source code: {SOURCE_URL}</Text>

        <Text style={styles.heading}>Includes</Text>
        {NOTICES.map(([name, licence, holder]) => (
          <Text key={name} style={styles.notice}>
            {name} ({licence}), © {holder}
          </Text>
        ))}
        <Text style={[styles.small, styles.after]}>
          Licence texts and source code for these are available with RosellaView's source.
        </Text>

        <Text style={styles.heading}>Trademarks</Text>
        <Text style={styles.small}>
          AirPlay is a trademark of Apple Inc., registered in the U.S. and other countries. RosellaView is not
          affiliated with or endorsed by Apple Inc.
        </Text>

        <Text style={styles.heading}>Settings</Text>
        <Pressable
          hasTVPreferredFocus
          onPress={() => {
            if (atEnd) {
              onCompatibilityMode(!compatibilityMode);
            }
          }}
          style={styles.checkRow}>
          {({focused}) => {
            const lit = focused && atEnd;
            return (
              <>
                <Text style={[styles.checkLabel, lit && styles.lit]}>Compatibility Mode</Text>
                <View style={[styles.box, lit && styles.boxLit]}>
                  {compatibilityMode ? <Text style={[styles.check, lit && styles.lit]}>✓</Text> : null}
                </View>
              </>
            );
          }}
        </Pressable>
        <Text style={styles.small}>
          Compatibility Mode plays mirrored video through the TV's standard video player. Try it if the picture doesn't
          appear. The brightest highlights and the darkest shadows will lose fidelity when this option is on.
        </Text>
        <Pressable
          onPress={() => {
            if (!atEnd) {
              return;
            }
            if (confirming) {
              setConfirming(false);
              onResetIdentity();
            } else {
              setConfirming(true);
            }
          }}
          focusable={atEnd}
          onFocus={() => (resetFocused.current = true)}
          onBlur={() => (resetFocused.current = false)}
          style={styles.resetRow}>
          {({focused}) => {
            const lit = focused && atEnd;
            return (
              <View style={[styles.button, lit && styles.buttonLit]}>
                <Text style={[styles.buttonText, lit && styles.buttonTextLit]}>
                  {confirming ? 'Press again to reset' : 'Reset Receiver Name'}
                </Text>
              </View>
            );
          }}
        </Pressable>
        <Text style={styles.small}>
          Resetting gives this TV a new name (now "{receiverName}") and forgets the devices paired with it; select it
          again on them afterwards.
        </Text>
      </ScrollView>
    </View>
  );
};

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#000', alignItems: 'center'},
  scroll: {flex: 1, width: '80%'},
  content: {paddingTop: 72, paddingBottom: 72},
  title: {color: '#fff', fontSize: 44, fontWeight: 'bold'},
  heading: {
    color: '#fff',
    fontSize: 28,
    fontWeight: 'bold',
    marginTop: 36,
    marginBottom: 10,
  },
  body: {color: '#aaa', fontSize: 24, marginTop: 4},
  small: {color: '#aaa', fontSize: 22, lineHeight: 32, marginBottom: 10},
  notice: {color: '#aaa', fontSize: 22, lineHeight: 32},
  after: {marginTop: 10},
  resetRow: {alignSelf: 'flex-start', paddingVertical: 8, marginTop: 16},
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
