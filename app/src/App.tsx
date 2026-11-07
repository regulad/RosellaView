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
import {MirrorPlayer} from './MirrorPlayer';

const RECEIVER_NAME = 'RosellaView';
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
  const [videoSize, setVideoSize] = useState<[number, number]>([0, 0]);
  const player = useRef<MirrorPlayer | null>(null);
  const surface = useRef<string | null>(null);

  useEffect(() => {
    const closePlayer = () => {
      player.current?.close();
      player.current = null;
    };

    Receiver.onVideo((kind, segment) => {
      if (kind === SegmentKind.Init && !player.current) {
        const p = new MirrorPlayer(log);
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
        case 'mirror_video_running':
          setMirroring(Boolean(e.running));
          if (!e.running) {
            closePlayer();
          }
          break;
        case 'conn_destroy':
          if ((e.connections as number) <= 0) {
            setMirroring(false);
            setSender(null);
            closePlayer();
          }
          break;
      }
    });

    log(`start(${RECEIVER_NAME}) -> ${Receiver.start(RECEIVER_NAME)}`);
    const stats = setInterval(() => {
      const s = Receiver.stats();
      if ((s.connections as number) > 0) {
        log(`stats ${JSON.stringify(s)} player ${player.current?.stats() ?? 'none'}`);
      }
    }, 5000);
    return () => {
      clearInterval(stats);
      Receiver.stop();
      Receiver.onEvent(null);
      Receiver.onVideo(null);
      closePlayer();
    };
  }, []);

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

  return (
    <View style={styles.root}>
      <Text style={styles.line}>Open Screen Mirroring and select</Text>
      <Text style={styles.name}>{RECEIVER_NAME}</Text>
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
