import { AppRegistry, LogBox } from 'react-native';
import { App } from './src/App';
import { initSentry } from './src/Sentry';
import { name as appName } from './app.json';

// Temporary workaround for problem with nested text
// not working currently.
LogBox.ignoreAllLogs();

initSentry();

AppRegistry.registerComponent(appName, () => App);
