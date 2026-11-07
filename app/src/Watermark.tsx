/*
 * Watermark: a white "RosellaView" plate at 80% opacity that bounces around
 * the screen, DVD-screensaver style, on top of the mirrored video until the
 * unlock is purchased.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import React, {useEffect, useRef} from 'react';
import {Animated, Dimensions, Easing, Image, StyleSheet, Text} from 'react-native';

const SCREEN = Dimensions.get('window');
// The pill holds a wordmark: the logo (docs/brand/RosellaView.svg, without
// its background, rendered at 2x) left of "RosellaView" in the system font.
const PLATE_W = 480;
const PLATE_H = 110;
const MARK = 84;
const LOGO = require('./assets/logo_mark.png');
const SPEED = 180; // px per second

export const Watermark = () => {
  const pos = useRef(new Animated.ValueXY({x: 60, y: 40})).current;
  const state = useRef({x: 60, y: 40, vx: 1, vy: 1});

  useEffect(() => {
    let stopped = false;
    // Animate straight wall-to-wall segments on the native driver, so JS only
    // runs at each bounce and never competes with video decoding.
    const next = () => {
      if (stopped) {
        return;
      }
      const s = state.current;
      const maxX = SCREEN.width - PLATE_W;
      const maxY = SCREEN.height - PLATE_H;
      const tx = (s.vx > 0 ? maxX - s.x : s.x) / SPEED;
      const ty = (s.vy > 0 ? maxY - s.y : s.y) / SPEED;
      const t = Math.max(0.001, Math.min(tx, ty));
      const nx = Math.min(maxX, Math.max(0, s.x + s.vx * SPEED * t));
      const ny = Math.min(maxY, Math.max(0, s.y + s.vy * SPEED * t));
      Animated.timing(pos, {
        toValue: {x: nx, y: ny},
        duration: t * 1000,
        easing: Easing.linear,
        useNativeDriver: true,
      }).start(({finished}) => {
        if (!finished) {
          return;
        }
        s.x = nx;
        s.y = ny;
        if (tx <= ty) {
          s.vx = -s.vx;
        }
        if (ty <= tx) {
          s.vy = -s.vy;
        }
        next();
      });
    };
    next();
    return () => {
      stopped = true;
      pos.stopAnimation();
    };
  }, [pos]);

  return (
    <Animated.View pointerEvents="none" style={[styles.plate, {transform: pos.getTranslateTransform()}]}>
      <Image source={LOGO} style={styles.mark} />
      <Text style={styles.text}>RosellaView</Text>
    </Animated.View>
  );
};

const styles = StyleSheet.create({
  plate: {
    position: 'absolute',
    left: 0,
    top: 0,
    width: PLATE_W,
    height: PLATE_H,
    opacity: 0.8,
    backgroundColor: '#fff',
    borderRadius: PLATE_H / 2, // pill-shaped
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'center',
    zIndex: 10,
  },
  mark: {width: MARK, height: MARK, marginRight: 10},
  text: {color: '#000', fontSize: 56, fontWeight: 'bold'},
});
