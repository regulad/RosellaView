/*
 * RosellaView: screen-mirroring receiver UI.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import React, {useEffect, useRef, useState} from 'react';
import {AppState, Dimensions, Pressable, StyleSheet, Text, View} from 'react-native';
import {AsyncStorage} from '@amazon-devices/react-native-kepler';
import {KeplerVideoSurfaceView} from '@amazon-devices/react-native-w3cmedia';
import {Receiver, ReceiverEvent, SegmentKind} from 'rosellaview-native';
import {RosellaSurface} from 'rosellaview-surface';
import {About} from './About';
import {AvDelayOverlay} from './AvDelayOverlay';
import {AvSync} from './AvSync';
import {KeepAwake} from './KeepAwake';
import {MirrorPlayer} from './MirrorPlayer';
import {NativeVideo} from './NativeVideo';
import {buyUnlock, cachedUnlocked, isUnlocked} from './Purchases';
import {log, receiverEvent, receiverStats, resetStats, warn} from './Telemetry';
import {Watermark} from './Watermark';

// The receiver advertises as "RosellaView <TAG>", TAG derived from the
// device's persistent ID (native/src/receiver.c).
const RECEIVER_BASE_NAME = 'RosellaView';
// Until the unlock is purchased the watermark is on: the bouncing plate over
// mirrored video, and a spoken "Preview." every 5 s of audio, mirroring
// included (mixed natively, native/src/audio.c).
const FLAC_MIME = 'audio/mp4; codecs="flac"';
const AAC_LC_MIME = 'audio/mp4; codecs="mp4a.40.2"';

/** AirPlay volume (-30..0 dB, -144 = mute) → linear 0..1. */
function airplayVolume(db: number): number {
  if (db <= -30) {
    return 0;
  }
  return Math.pow(10, Math.min(0, db) / 20);
}
const SCREEN = Dimensions.get('window');
// Stats run every 5 s; with MSE video, a keyframe is requested every other
// tick (10 s), inside MirrorPlayer's 10 s back buffer.
const KEYFRAME_EVERY_TICKS = 2;
// Compatibility Mode (About screen): MSE video instead of the native pipeline.
const COMPATIBILITY_KEY = 'compatibilityMode';

/** Largest box of the video's aspect ratio that fits the screen, centred. */
function fit(w: number, h: number) {
  if (!w || !h) {
    return {left: 0, top: 0, width: SCREEN.width, height: SCREEN.height};
  }
  const scale = Math.min(SCREEN.width / w, SCREEN.height / h);
  const width = Math.round(w * scale);
  const height = Math.round(h * scale);
  return {left: Math.round((SCREEN.width - width) / 2), top: Math.round((SCREEN.height - height) / 2), width, height};
}

export const App = () => {
  const [mirroring, setMirroring] = useState(false);
  const [sender, setSender] = useState<string | null>(null);
  const [receiverName, setReceiverName] = useState(RECEIVER_BASE_NAME);
  const [audioOnly, setAudioOnly] = useState(false);
  const [unlocked, setUnlocked] = useState(false);
  const [buying, setBuying] = useState(false);
  const [videoSize, setVideoSize] = useState<[number, number]>([0, 0]);
  const player = useRef<MirrorPlayer | null>(null);
  // Video goes to the native pipeline (RosellaSurface) unless the viewer has
  // turned on Compatibility Mode (About screen); then to MSE (MirrorPlayer).
  const [compatibility, setCompatibility] = useState(false);
  const useNative = useRef(true);
  useNative.current = !compatibility;
  const nativeVideo = useRef<NativeVideo | null>(null);
  const [showAbout, setShowAbout] = useState(false);
  const audio = useRef<MirrorPlayer | null>(null);
  const audioCt = useRef(0);
  const volume = useRef(1);
  const surface = useRef<string | null>(null);
  const keepAwake = useRef(new KeepAwake(log));
  const avSync = useRef(new AvSync()).current;
  const [avDelay, setAvDelay] = useState(0);
  useEffect(() => {
    const closePlayer = () => {
      player.current?.close();
      player.current = null;
    };
    const closeAudio = () => {
      audio.current?.close();
      audio.current = null;
    };

    Receiver.onAudio((kind, segment, senderMs) => {
      if (kind === SegmentKind.Init) {
        // Each audio stream (mirroring, or music) starts with a new init segment.
        closeAudio();
        const a = new MirrorPlayer(log, {kind: 'audio', mime: audioCt.current === 4 ? AAC_LC_MIME : FLAC_MIME});
        audio.current = a;
        a.open()
          .then(() => a.setVolume(volume.current))
          .catch((e) => log(`audio open failed: ${String(e)}`));
      }
      audio.current?.push(segment, kind === SegmentKind.Media, senderMs);
    });

    // A stalled video player is replaced: the sender is asked for a keyframe,
    // which starts a new player (see onVideo below). At most every 10 s.
    let lastRecovery = 0;
    const recoverVideo = (p: MirrorPlayer) => {
      const now = Date.now();
      warn('video player stalled', {player: p.stats(), receiver: Receiver.stats()});
      if (now - lastRecovery >= 10000) {
        lastRecovery = now;
        closePlayer();
        Receiver.restartVideo();
      }
    };

    // MSE video only: native video bypasses JS (the surface takes it directly).
    Receiver.onVideo((kind, segment, senderMs) => {
      if (useNative.current) {
        return;
      }
      if (kind === SegmentKind.Init && !player.current) {
        const p = new MirrorPlayer(log, {kind: 'video'}, recoverVideo);
        player.current = p;
        p.setSurface(surface.current);
        p.open().catch((e) => log(`player open failed: ${String(e)}`));
      }
      player.current?.push(segment, kind === SegmentKind.Media, senderMs);
    });

    Receiver.onEvent((e: ReceiverEvent) => {
      receiverEvent(e);
      switch (e.type) {
        case 'started':
          setReceiverName(String(e.name));
          break;
        case 'client_request':
          setSender(String(e.name));
          avSync.useSender(String(e.deviceid)).then((ms) => {
            setAvDelay(ms);
            log(`av delay for ${String(e.model)}: ${ms}ms`);
          });
          break;
        case 'video_format': {
          const [w, h] = e.source as [number, number];
          setVideoSize([w, h]);
          break;
        }
        case 'video_init':
          // Sent for every new SPS, including rotation, which libairplay
          // doesn't report as a video_format change.
          if ((e.width as number) > 0 && (e.height as number) > 0) {
            setVideoSize([e.width as number, e.height as number]);
          }
          break;
        case 'audio_format':
          audioCt.current = e.ct as number;
          setAudioOnly(!e.using_screen);
          break;
        case 'volume':
          volume.current = airplayVolume(e.db as number);
          audio.current?.setVolume(volume.current);
          break;
        case 'mirror_video_running':
          setMirroring(Boolean(e.running));
          if (e.running) {
            setAudioOnly(false);
            // Each session starts from the default latency.
            nativeVideo.current = useNative.current ? new NativeVideo() : null;
          }
          if (!e.running) {
            closePlayer();
            nativeVideo.current = null;
          }
          break;
        case 'conn_destroy':
          if ((e.connections as number) <= 0) {
            setMirroring(false);
            setSender(null);
            setAudioOnly(false);
            closePlayer();
            closeAudio();
            nativeVideo.current = null;
            resetStats();
          }
          break;
      }
    });

    log(`start(${RECEIVER_BASE_NAME}) -> ${Receiver.start(RECEIVER_BASE_NAME)}`);
    keepAwake.current.set(true);
    let statsTicks = 0;
    const stats = setInterval(() => {
      const s = Receiver.stats();
      if ((s.connections as number) > 0) {
        const nv = nativeVideo.current;
        receiverStats(
          s,
          nv ? nv.metrics() : player.current?.metrics() ?? null,
          audio.current?.metrics() ?? null,
          avSync,
          nv ? nv.state() : null,
        );
        // iOS and macOS send one IDR per session unless asked; the player can
        // only drop played video (MirrorPlayer.trim) back to an IDR, so ask
        // for one every KEYFRAME_EVERY_TICKS (DECISIONS.md D7).
        if (player.current && ++statsTicks % KEYFRAME_EVERY_TICKS === 0 && !Receiver.requestKeyframe()) {
          warn('keyframe request not sent (no event channel)');
        }
      }
    }, 5000);
    const sync = setInterval(() => avSync.tick(nativeVideo.current ?? player.current, audio.current), 1000);
    return () => {
      clearInterval(stats);
      clearInterval(sync);
      Receiver.stop();
      Receiver.onEvent(null);
      Receiver.onVideo(null);
      Receiver.onAudio(null);
      closePlayer();
      closeAudio();
      keepAwake.current.set(false);
    };
  }, []);

  // Entitlement: the cached answer at once, then the Appstore's at launch and
  // on every return to the foreground, so a purchase or refund made elsewhere
  // is picked up without a restart. If the Appstore can't answer, the cached
  // answer stands.
  useEffect(() => {
    let answered = false; // a live answer beats the cached one
    cachedUnlocked().then((u) => {
      if (u && !answered) {
        setUnlocked(true);
      }
    });
    const refresh = () =>
      isUnlocked(log)
        .then((u) => {
          log(`unlocked=${u === null ? 'unknown (kept cached)' : u}`);
          if (u !== null) {
            answered = true;
            setUnlocked(u);
          }
        })
        .catch((e) => log(`entitlement check failed: ${String(e)}`));
    refresh();
    const sub = AppState.addEventListener('change', (state) => {
      if (state === 'active') {
        refresh();
      }
    });
    return () => sub.remove();
  }, []);

  useEffect(() => {
    Receiver.setWatermark(!unlocked);
  }, [unlocked]);

  useEffect(() => {
    AsyncStorage.getItem(COMPATIBILITY_KEY)
      .then((v) => setCompatibility(v === '1'))
      .catch(() => undefined);
  }, []);
  const onCompatibilityMode = (on: boolean) => {
    setCompatibility(on);
    log(`compatibility mode ${on ? 'on' : 'off'}`);
    AsyncStorage.setItem(COMPATIBILITY_KEY, on ? '1' : '0').catch(() => undefined);
  };

  const onRemoveWatermark = () => {
    if (buying) {
      return;
    }
    setBuying(true);
    buyUnlock(log)
      .then((ok) => {
        if (ok) {
          setUnlocked(true);
        }
      })
      .catch((e) => log(`purchase failed: ${String(e)}`))
      .finally(() => setBuying(false));
  };

  // The silent keep-awake loop runs only while nothing is connected, so it
  // never competes with the real players for audio focus.
  useEffect(() => {
    keepAwake.current.set(sender === null);
  }, [sender]);

  if (mirroring) {
    return (
      <View style={styles.root}>
        {!compatibility ? (
          <RosellaSurface style={styles.fill} />
        ) : (
          <KeplerVideoSurfaceView
            style={[styles.video, fit(videoSize[0], videoSize[1])]}
            onSurfaceViewCreated={(handle: string) => {
              surface.current = handle;
              player.current?.setSurface(handle);
            }}
            onSurfaceViewDestroyed={() => {
              surface.current = null;
              player.current?.setSurface(null);
            }}
          />
        )}
        {unlocked ? null : <Watermark />}
        <AvDelayOverlay
          active={mirroring}
          delayMs={avDelay}
          onChange={(ms) => {
            avSync.setDelay(ms);
            setAvDelay(avSync.delayMs);
            log(`av delay set to ${avSync.delayMs}ms`);
          }}
        />
      </View>
    );
  }

  if (audioOnly && sender) {
    return (
      <View style={styles.root}>
        <Text style={styles.line}>Playing audio from</Text>
        <Text style={styles.name}>{sender}</Text>
      </View>
    );
  }

  if (showAbout) {
    return (
      <About
        compatibilityMode={compatibility}
        onCompatibilityMode={onCompatibilityMode}
        onClose={() => setShowAbout(false)}
      />
    );
  }

  return (
    <View style={styles.root}>
      {/* Referential use of Apple's mark for its own sender-side feature;
          the AirPlay trademark notice belongs on the licence screen. */}
      <Text style={styles.line}>Open AirPlay® and select</Text>
      <Text style={styles.name}>{receiverName}</Text>
      {sender ? <Text style={styles.subtle}>Connecting to {sender}…</Text> : null}
      {unlocked ? null : (
        // The only focusable element: unfocused until the first remote press,
        // and pinned to the bottom so the instructions stay centred.
        <Pressable
          disabled={buying}
          onPress={onRemoveWatermark}
          style={({focused}) => [styles.button, focused && styles.buttonFocused]}>
          {({focused}) => (
            <Text style={[styles.buttonText, focused && styles.buttonTextFocused]}>
              {buying ? 'Opening Appstore…' : 'Remove audiovisual watermark'}
            </Text>
          )}
        </Pressable>
      )}
      <Pressable onPress={() => setShowAbout(true)} style={styles.about}>
        {({focused}) => <Text style={[styles.aboutText, focused && styles.aboutTextFocused]}>About & privacy</Text>}
      </Pressable>
    </View>
  );
};

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#000', alignItems: 'center', justifyContent: 'center'},
  video: {position: 'absolute'},
  fill: {position: 'absolute', left: 0, top: 0, width: SCREEN.width, height: SCREEN.height},
  line: {color: '#ddd', fontSize: 40},
  name: {color: '#fff', fontSize: 64, fontWeight: 'bold', marginTop: 12},
  subtle: {color: '#888', fontSize: 28, marginTop: 24},
  button: {
    position: 'absolute',
    bottom: 104,
    paddingHorizontal: 40,
    paddingVertical: 16,
    borderRadius: 40,
    borderWidth: 3,
    borderColor: '#666',
  },
  buttonFocused: {backgroundColor: '#fff', borderColor: '#fff'},
  buttonText: {color: '#aaa', fontSize: 30},
  buttonTextFocused: {color: '#000'},
  about: {position: 'absolute', bottom: 44, paddingHorizontal: 12, paddingVertical: 4},
  aboutText: {color: '#777', fontSize: 20, textDecorationLine: 'underline'},
  aboutTextFocused: {color: '#fff'},
});
