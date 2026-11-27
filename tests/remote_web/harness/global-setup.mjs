// Before any test: the build they run has to exist.

import fs from 'node:fs';

import { appBinary, buildDir, testsBinary } from './app.mjs';

export default function globalSetup() {
  for (const binary of [appBinary, testsBinary]) {
    if (!fs.existsSync(binary)) {
      throw new Error(`${binary} is missing. Build the project first (cmake --build `
        + `${buildDir}), or set SEVENTHSLIDE_BUILD_DIR to the build to test.`);
    }
  }
}
