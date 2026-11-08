// ESLint for the app and the two native packages' JS faces. Formatting is
// Prettier's job (eslint-config-prettier turns the overlapping rules off).
//
// Copyright (C) 2026 Parker Wahle
// SPDX-License-Identifier: AGPL-3.0-or-later
import js from '@eslint/js';
import prettier from 'eslint-config-prettier';
import reactHooks from 'eslint-plugin-react-hooks';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  {
    ignores: ['third_party/', '**/node_modules/', '**/build/', '**/dist/', 'app/src/buildInfo.ts'],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ['app/**/*.{ts,tsx,js}', 'native/js/**/*.ts', 'native-view/js/**/*.ts'],
    plugins: {'react-hooks': reactHooks},
    languageOptions: {globals: {...globals.es2022}},
    rules: {
      ...reactHooks.configs.recommended.rules,
      '@typescript-eslint/no-unused-vars': ['error', {argsIgnorePattern: '^_', varsIgnorePattern: '^_'}],
      // React Native loads image and other assets with require().
      '@typescript-eslint/no-require-imports': ['error', {allow: ['\\.(png|jpg|mp4|pcm)$']}],
    },
  },
  {
    // Build and config scripts run under Node.
    files: [
      '**/*.config.js',
      '**/*.config.mjs',
      'app/metro.config.js',
      'app/babel.config.js',
      '**/react-native.config.js',
    ],
    languageOptions: {globals: {...globals.node}},
    rules: {'@typescript-eslint/no-require-imports': 'off'},
  },
  prettier,
);
