/*
 * RosellaView: screen-mirroring receiver UI.
 *
 * Copyright (C) 2026 Parker Wahle
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
import React, {useEffect, useState} from 'react';
import {StyleSheet, Text, View} from 'react-native';
import {Receiver, ReceiverEvent} from 'rosellaview-native';

const RECEIVER_NAME = 'RosellaView';

// Receiver events go to the device log (loggingctl) for now; the device
// logger keeps only the first line of each message.
const log = (msg: string) => console.log(`ROSELLAVIEW ${msg}`);

export const App = () => {
  const [mirroring, setMirroring] = useState(false);
  const [sender, setSender] = useState<string | null>(null);

  useEffect(() => {
    Receiver.onEvent((e: ReceiverEvent) => {
      if (e.type !== 'log' || (e.level as number) <= 5) {
        log(JSON.stringify(e));
      }
      switch (e.type) {
        case 'client_request':
          setSender(String(e.name));
          break;
        case 'mirror_video_running':
          setMirroring(Boolean(e.running));
          break;
        case 'conn_destroy':
          if ((e.connections as number) <= 0) {
            setMirroring(false);
            setSender(null);
          }
          break;
      }
    });
    log(`start(${RECEIVER_NAME}) -> ${Receiver.start(RECEIVER_NAME)}`);
    const stats = setInterval(() => {
      const s = Receiver.stats();
      if ((s.connections as number) > 0) {
        log(`stats ${JSON.stringify(s)}`);
      }
    }, 5000);
    return () => {
      clearInterval(stats);
      Receiver.stop();
      Receiver.onEvent(null);
    };
  }, []);

  if (mirroring) {
    // Phase 2 replaces this with the video surface.
    return (
      <View style={styles.root}>
        <Text style={styles.subtle}>Receiving from {sender ?? 'device'}…</Text>
      </View>
    );
  }

  return (
    <View style={styles.root}>
      <Text style={styles.line}>Open Screen Mirroring and select</Text>
      <Text style={styles.name}>{RECEIVER_NAME}</Text>
    </View>
  );
};

const styles = StyleSheet.create({
  root: {flex: 1, backgroundColor: '#000', alignItems: 'center', justifyContent: 'center'},
  line: {color: '#ddd', fontSize: 40},
  name: {color: '#fff', fontSize: 64, fontWeight: 'bold', marginTop: 12},
  subtle: {color: '#888', fontSize: 28},
});
