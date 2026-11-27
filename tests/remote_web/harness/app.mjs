// One SeventhSlide per test: a fresh library, built by the test binary
// (tests/remote_web_fixture.cpp), served by the real app, offscreen.

import { execFile, spawn } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';

const here = path.dirname(fileURLToPath(import.meta.url));

/** The CMake build whose app and test binary are run. */
export const buildDir = path.resolve(
  process.env.SEVENTHSLIDE_BUILD_DIR || path.join(here, '..', '..', '..', 'build'));
/** The programs, as CMake names them (ctest passes the paths); else where a
 *  single-config build puts them: an .exe on Windows, inside the bundle on
 *  macOS. */
const exe = process.platform === 'win32' ? '.exe' : '';
export const appBinary = process.env.SEVENTHSLIDE_APP_BINARY || path.join(
  buildDir, 'bin',
  process.platform === 'darwin' ? 'seventhslide.app/Contents/MacOS/seventhslide' : `seventhslide${exe}`);
export const testsBinary = process.env.SEVENTHSLIDE_TESTS_BINARY
  || path.join(buildDir, 'bin', `seventhslide_tests_remote${exe}`);

const START_TIMEOUT_MS = 20_000;
const STOP_TIMEOUT_MS = 10_000;

/** A port nothing is listening on now. The app binds it a moment later. */
async function freePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const { port } = server.address();
  await new Promise((resolve) => server.close(resolve));
  return port;
}

/** Each process its own config, cache and data, and no sound: with no
 *  sound server or ALSA to reach, the Audio Mixer runs without a device (which
 *  still keeps time). */
function isolatedEnv(dir) {
  return {
    ...process.env,
    QT_QPA_PLATFORM: 'offscreen',
    SEVENTHSLIDE_DATA_DIR: path.join(dir, 'data'),
    // The app's settings (src/app/main.cpp); XDG_CONFIG_HOME only moves
    // them on Linux.
    SEVENTHSLIDE_SETTINGS_DIR: path.join(dir, 'config'),
    XDG_CONFIG_HOME: path.join(dir, 'config'),
    XDG_CACHE_HOME: path.join(dir, 'cache'),
    XDG_DATA_HOME: path.join(dir, 'share'),
    PULSE_SERVER: 'unix:/nonexistent/pulse',
    PIPEWIRE_REMOTE: '/nonexistent/pipewire',
    ALSA_CONFIG_PATH: '/dev/null',
  };
}

/** Build the test's library in ``dir`` for ``port``; answers what it holds.
 *  ``options`` go to the fixture (tests/remote_web_fixture.cpp): ``songs``
 *  adds that many. */
async function buildLibrary(dir, port, options) {
  try {
    await promisify(execFile)(testsBinary, ['[.remote_web]'], {
      env: {
        ...isolatedEnv(dir),
        SEVENTHSLIDE_REMOTE_FIXTURE_DIR: dir,
        SEVENTHSLIDE_REMOTE_FIXTURE_PORT: String(port),
        SEVENTHSLIDE_REMOTE_FIXTURE_SONGS: String(options.songs || 0),
      },
    });
  } catch (err) {
    throw new Error(`Building the test library failed:\n${err.stdout || ''}${err.stderr || ''}`,
      { cause: err });
  }
  return JSON.parse(fs.readFileSync(path.join(dir, 'fixture.json'), 'utf8'));
}

/** The app serving one test's library. */
export class App {
  constructor(dir, port, fixture) {
    this.dir = dir;
    this.port = port;
    /** What the library holds (tests/remote_web_fixture.cpp). */
    this.fixture = fixture;
    this.logPath = path.join(dir, 'app.log');
    this.process = null;
    /** Set when the app exits without being asked to. */
    this.unexpectedExit = null;
  }

  get url() {
    return `http://127.0.0.1:${this.port}`;
  }

  /** Start the app and wait until the remote answers. */
  async start() {
    const log = fs.openSync(this.logPath, 'a');
    const child = spawn(appBinary, [], {
      env: isolatedEnv(this.dir),
      stdio: ['ignore', log, log],
    });
    fs.closeSync(log);
    this.process = child;
    this.unexpectedExit = null;
    child.once('exit', (code, signal) => {
      if (this.process === child) {
        this.unexpectedExit = signal ? `signal ${signal}` : `exit code ${code}`;
        this.process = null;
      }
    });

    const deadline = Date.now() + START_TIMEOUT_MS;
    while (Date.now() < deadline) {
      if (this.unexpectedExit) {
        throw new Error(`The app exited while starting (${this.unexpectedExit}):\n${this.logTail()}`);
      }
      try {
        const res = await fetch(`${this.url}/api/hello`);
        if (res.ok) return;
      } catch { /* not listening yet */ }
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    await this.stop();
    throw new Error(`The remote did not answer within ${START_TIMEOUT_MS} ms:\n${this.logTail()}`);
  }

  /** Stop the app (SIGTERM, then SIGKILL if it hangs). On Windows both end
   *  it at once, with nothing saved at quitting: no test here needs that. */
  async stop() {
    const child = this.process;
    if (!child) return;
    this.process = null;
    const exited = new Promise((resolve) => child.once('exit', resolve));
    child.kill('SIGTERM');
    const timer = setTimeout(() => child.kill('SIGKILL'), STOP_TIMEOUT_MS);
    await exited;
    clearTimeout(timer);
  }

  /** Stop and start again on the same library and port. */
  async restart() {
    await this.stop();
    await this.start();
  }

  logTail(lines = 40) {
    try {
      return fs.readFileSync(this.logPath, 'utf8').split('\n').slice(-lines).join('\n');
    } catch {
      return '(no log)';
    }
  }
}

/** A fresh library in ``dir`` and the app serving it (``options``: see
 *  buildLibrary). */
export async function launchApp(dir, options = {}) {
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const port = await freePort();
  const fixture = await buildLibrary(dir, port, options);
  const app = new App(dir, port, fixture);
  await app.start();
  return app;
}
