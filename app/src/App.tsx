/*
 * RosellaView: screen-mirroring receiver UI.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import React, {useEffect, useRef, useState} from 'react';
import {Dimensions, StyleSheet, Text, View} from 'react-native';
import {KeplerVideoSurfaceView} from '@amazon-devices/react-native-w3cmedia';
import {Receiver, ReceiverEvent, SegmentKind} from 'rosellaview-native';
import {KeepAwake} from './KeepAwake';
import {MirrorPlayer} from './MirrorPlayer';

// The receiver advertises as "RosellaView <TAG>", TAG derived from the
// device's persistent ID (native/src/receiver.c).
const RECEIVER_BASE_NAME = 'RosellaView';
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

// Receiver events go to the device log (loggingctl) for now; the device
// logger keeps only the first line of each message.
const log = (msg: string) => console.log(`ROSELLAVIEW ${msg}`);

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
  const [videoSize, setVideoSize] = useState<[number, number]>([0, 0]);
  const player = useRef<MirrorPlayer | null>(null);
  const audio = useRef<MirrorPlayer | null>(null);
  const audioCt = useRef(0);
  const volume = useRef(1);
  const surface = useRef<string | null>(null);
  const keepAwake = useRef(new KeepAwake(log));

  useEffect(() => {
    const closePlayer = () => {
      player.current?.close();
      player.current = null;
    };
    const closeAudio = () => {
      audio.current?.close();
      audio.current = null;
    };

    Receiver.onAudio((kind, segment) => {
      if (kind === SegmentKind.Init) {
        // Each audio stream (mirroring, or music) starts with a new init segment.
        closeAudio();
        const a = new MirrorPlayer(log, {kind: 'audio', mime: audioCt.current === 4 ? AAC_LC_MIME : FLAC_MIME});
        audio.current = a;
        a.open()
          .then(() => a.setVolume(volume.current))
          .catch((e) => log(`audio open failed: ${String(e)}`));
      }
      audio.current?.push(segment, kind === SegmentKind.Media);
    });

    // A stalled video player is rebuilt and fed the current GOP again.
    let lastRecovery = 0;
    const recoverVideo = () => {
      const now = Date.now();
      if (now - lastRecovery < 10000) {
        return;
      }
      lastRecovery = now;
      closePlayer();
      log(`video resync -> ${Receiver.resyncVideo()}`);
    };

    Receiver.onVideo((kind, segment) => {
      if (kind === SegmentKind.Init && !player.current) {
        const p = new MirrorPlayer(log, {kind: 'video'}, recoverVideo);
        player.current = p;
        p.setSurface(surface.current);
        p.open().catch((e) => log(`player open failed: ${String(e)}`));
      }
      player.current?.push(segment, kind === SegmentKind.Media);
    });

    Receiver.onEvent((e: ReceiverEvent) => {
      if (e.type !== 'log' || (e.level as number) <= 5) {
        log(JSON.stringify(e));
      }
      switch (e.type) {
        case 'started':
          setReceiverName(String(e.name));
          break;
        case 'client_request':
          setSender(String(e.name));
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
          }
          if (!e.running) {
            closePlayer();
          }
          break;
        case 'conn_destroy':
          if ((e.connections as number) <= 0) {
            setMirroring(false);
            setSender(null);
            setAudioOnly(false);
            closePlayer();
            closeAudio();
          }
          break;
      }
    });

    log(`start(${RECEIVER_BASE_NAME}) -> ${Receiver.start(RECEIVER_BASE_NAME)}`);
    keepAwake.current.set(true);
    const stats = setInterval(() => {
      const s = Receiver.stats();
      if ((s.connections as number) > 0) {
        log(`stats ${JSON.stringify(s)} video ${player.current?.stats() ?? 'none'} audio ${audio.current?.stats() ?? 'none'}`);
      }
    }, 5000);
    return () => {
      clearInterval(stats);
      Receiver.stop();
      Receiver.onEvent(null);
      Receiver.onVideo(null);
      Receiver.onAudio(null);
      closePlayer();
      closeAudio();
      keepAwake.current.set(false);
    };
  }, []);

  // The silent keep-awake loop runs only while nothing is connected, so it
  // never competes with the real players for audio focus.
  useEffect(() => {
    keepAwake.current.set(sender === null);
  }, [sender]);

  if (mirroring) {
    return (
      <View style={styles.root}>
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

  return (
    <View style={styles.root}>
      {/* Referential use of Apple's mark for its own sender-side feature;
          the AirPlay trademark notice belongs on the licence screen. */}
      <Text style={styles.line}>Open AirPlay® and select</Text>
      <Text style={styles.name}>{receiverName}</Text>
      {sender ? <Text style={styles.subtle}>Connecting to {sender}…</Text> : null}
    </View>
  );
};

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#000', alignItems: 'center', justifyContent: 'center'},
  video: {position: 'absolute'},
  line: {color: '#ddd', fontSize: 40},
  name: {color: '#fff', fontSize: 64, fontWeight: 'bold', marginTop: 12},
  subtle: {color: '#888', fontSize: 28, marginTop: 24},
});
