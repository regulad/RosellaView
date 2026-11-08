/*
 * AvDelayOverlay: the audio delay control for lip sync. Left/right on the
 * remote while mirroring show it and move the delay in STEP_MS steps; it fades
 * out after HIDE_AFTER_MS without a press. AvSync applies and saves the value.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import {useTVEventHandler} from '@amazon-devices/react-native-kepler';
import type {HWEvent} from '@amazon-devices/react-native-kepler';
import React, {useEffect, useRef} from 'react';
import {Animated, Dimensions, Easing, StyleSheet, Text, View} from 'react-native';
import {AV_DELAY_MAX_MS, AV_DELAY_MIN_MS} from './AvSync';

const STEP_MS = 20;
const HIDE_AFTER_MS = 3000;
const FADE_IN_MS = 150;
const FADE_OUT_MS = 700;
const BAR_W = 520;
const KEY_UP = 1;

type Props = {
  /** Only while mirroring: elsewhere the arrow keys move focus. */
  active: boolean;
  delayMs: number;
  onChange: (ms: number) => void;
};

export const AvDelayOverlay = ({active, delayMs, onChange}: Props) => {
  const opacity = useRef(new Animated.Value(0)).current;
  const hideTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const latest = useRef({active, delayMs, onChange});
  latest.current = {active, delayMs, onChange};

  const show = () => {
    Animated.timing(opacity, {toValue: 1, duration: FADE_IN_MS, useNativeDriver: true}).start();
    if (hideTimer.current) {
      clearTimeout(hideTimer.current);
    }
    hideTimer.current = setTimeout(() => {
      Animated.timing(opacity, {
        toValue: 0,
        duration: FADE_OUT_MS,
        easing: Easing.out(Easing.quad),
        useNativeDriver: true,
      }).start();
    }, HIDE_AFTER_MS);
  };

  useTVEventHandler((e: HWEvent) => {
    const {active: on, delayMs: d, onChange: change} = latest.current;
    if (!on || e.eventKeyAction === KEY_UP) {
      return;
    }
    if (e.eventType === 'left' || e.eventType === 'right') {
      const next = Math.min(
        AV_DELAY_MAX_MS,
        Math.max(AV_DELAY_MIN_MS, d + (e.eventType === 'right' ? STEP_MS : -STEP_MS)),
      );
      change(next);
      show();
    }
  });

  useEffect(() => {
    if (!active) {
      opacity.setValue(0);
    }
    return () => {
      if (hideTimer.current) {
        clearTimeout(hideTimer.current);
      }
    };
  }, [active, opacity]);

  const frac = (delayMs - AV_DELAY_MIN_MS) / (AV_DELAY_MAX_MS - AV_DELAY_MIN_MS);
  const value = `${delayMs > 0 ? '+' : ''}${delayMs}ms`;
  return (
    <Animated.View pointerEvents="none" style={[styles.overlay, {opacity}]}>
      <Text style={styles.label}>
        Audio Delay <Text style={styles.value}>{value}</Text>
      </Text>
      <View style={styles.bar}>
        <View style={styles.center} />
        <View style={[styles.knob, {left: frac * BAR_W - KNOB / 2}]} />
      </View>
    </Animated.View>
  );
};

const SCREEN = Dimensions.get('window');
const KNOB = 28;
const LABEL_H = 56;

const styles = StyleSheet.create({
  // The bar sits on the line one sixth of the screen up from the bottom.
  overlay: {
    position: 'absolute',
    top: (SCREEN.height * 5) / 6 - LABEL_H,
    alignSelf: 'center',
    alignItems: 'center',
    zIndex: 20,
  },
  label: {height: LABEL_H, color: '#fff', fontSize: 30, fontWeight: 'bold'},
  value: {color: '#a0a0a0', fontWeight: 'bold'},
  bar: {width: BAR_W, height: 8, borderRadius: 4, backgroundColor: 'rgba(160,160,160,0.7)', justifyContent: 'center'},
  center: {position: 'absolute', left: BAR_W / 2 - 1, width: 2, height: 22, top: -7, backgroundColor: '#fff'},
  knob: {position: 'absolute', width: KNOB, height: KNOB, borderRadius: KNOB / 2, top: -10, backgroundColor: '#fff'},
});
