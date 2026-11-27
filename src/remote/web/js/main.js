/* SeventhSlide web remote.
 *
 * Left rail (Service + Library), centre controller (and a stack's Preview beside what is
 * live), click-to-select / double-click-to-go-live. A remote for playback: choosing and editing services, the library, outputs and
 * settings stay on the console.
 *
 * This module starts the page and redraws it from what the server sends. Each part of the
 * page is a module beside it; connection.js talks to the server, and state.js holds what
 * the page knows.
 */

import { startConnection } from './connection.js';
import { wirePanels } from './panels.js';
import {
  controllerKeydown, renderAdvance, renderLive, resetFollow, wireController,
} from './controller.js';
import { wireStacks } from './stacks.js';
import { wirePreview } from './preview.js';
import { patchServiceChrome, renderService } from './service.js';
import { loadLibraryIfEmpty, renderLibTabs, wireLibrary } from './library.js';
import { wireBible } from './bible.js';
import { menuKeydown, wireMenus } from './menus.js';
import { listKeydown } from './gestures.js';

function renderAll() {
  renderService();
  renderLive();
  renderAdvance();
}

function renderPatched(keys) {
  const touched = new Set(keys);
  if (touched.has('service') || touched.has('stacks')) {
    renderService();
  } else if (touched.has('session') || touched.has('images')) {
    patchServiceChrome();
  }
  if (['session', 'lines', 'stacks', 'images', 'advance', 'preview', 'previewLines',
    'previewImages'].some((k) => touched.has(k))) {
    renderLive();
  }
  if (touched.has('advance') || touched.has('stacks') || touched.has('session')) {
    renderAdvance();
  }
}

wireMenus();
wireController();
wirePreview();
wireStacks();
wireLibrary();
wireBible();
wirePanels();

window.addEventListener('keydown', (e) => {
  if (menuKeydown(e)) return;
  if (listKeydown(e)) return;
  if (e.target.matches('input, select, textarea')) return;
  controllerKeydown(e);
});

renderLibTabs();
startConnection({
  snapshot() {
    resetFollow();
    renderAll();
  },
  patch: renderPatched,
  refused: renderLive,
  signedIn: loadLibraryIfEmpty,
});
