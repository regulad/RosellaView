import { AppRegistry, LogBox } from 'react-native';
import { App } from './src/App';
import { installCrashJournal } from './src/CrashJournal';
import { name as appName } from './app.json';

// Temporary workaround for problem with nested text
// not working currently.
LogBox.ignoreAllLogs();

installCrashJournal();

AppRegistry.registerComponent(appName, () => App);
