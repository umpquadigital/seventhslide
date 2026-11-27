// The server: signing in, the socket and what arrives on it, commands, and
// the HTTP reads. It knows no view; startConnection is told what to call.

import { $, toast } from './dom.js';
import { dropPendingCursors, focusedStackId, state, syncPendingCursor } from './state.js';

let socket = null;
// The reconnect waiting to start (onclose sets it), or 0.
let reconnectTimer = 0;
let backoff = 1000;
let cmdSeq = 0;
const pendingAcks = new Map();
// main.js's handlers, from startConnection.
let views = null;

function authHeaders(extra) {
  const h = Object.assign({}, extra || {});
  if (state.token) h.Authorization = `Bearer ${state.token}`;
  return h;
}

export async function api(path, options) {
  const res = await fetch(path, Object.assign({}, options, {
    headers: authHeaders((options || {}).headers),
  }));
  if (res.status === 401) {
    showGate('Session expired. Enter the PIN again.');
    throw new Error('unauthorized');
  }
  return res.json();
}

/* Send a command; answers its ack. Every command is answered: one that
   cannot be sent, or is still unanswered when the connection drops, with a
   refusal, so a caller waiting on it goes on and nothing needs catching. */
export function send(name, args) {
  if (!socket || socket.readyState !== WebSocket.OPEN) {
    toast('Not connected');
    dropPendingCursor();
    return Promise.resolve(refusal('Not connected'));
  }
  const id = `c${++cmdSeq}`;
  return new Promise((resolve) => {
    pendingAcks.set(id, resolve);
    socket.send(JSON.stringify({ type: 'cmd', id, name, args: args || {} }));
  });
}

function refusal(error) {
  return { type: 'ack', ok: false, error };
}

// A line the page showed ahead of the server, for a command that will not
// arrive: show the server's line again.
function dropPendingCursor() {
  if (dropPendingCursors()) views.refused();
}

// This device's stack tab, kept across a dropped connection and a reload
// (sessionStorage is per browser tab), so reconnecting does not move it to
// the console's.
function savedTab() {
  try { return sessionStorage.getItem('ss_tab') || ''; } catch { return ''; }
}
function saveTab(id) {
  try { if (id) sessionStorage.setItem('ss_tab', id); } catch { /* storage off */ }
}

// Answer every command still waiting with a refusal: the connection it went
// out on is gone.
function refuseWaiting() {
  for (const wait of pendingAcks.values()) wait(refusal('Not connected'));
  pendingAcks.clear();
  dropPendingCursor();
}

// Close the socket, if there is one, and cancel a reconnect waiting. Its
// handlers go first, so its closing starts no reconnect of its own.
function closeSocket() {
  clearTimeout(reconnectTimer);
  reconnectTimer = 0;
  if (!socket) return;
  const old = socket;
  socket = null;
  old.onopen = null;
  old.onmessage = null;
  old.onclose = null;
  old.onerror = null;
  old.close();
  refuseWaiting();
}

// Open this device's one socket, closing any it had.
function connect() {
  closeSocket();
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const params = new URLSearchParams();
  if (state.token) params.set('token', state.token);
  const tab = focusedStackId() || savedTab();
  if (tab) params.set('tab', tab);
  const q = params.toString() ? `?${params}` : '';
  const ws = new WebSocket(`${proto}//${location.host}/ws${q}`);
  socket = ws;

  ws.onopen = () => {
    backoff = 1000;
    $('offline').hidden = true;
    acquireWakeLock();
  };

  ws.onmessage = (event) => {
    let msg;
    try { msg = JSON.parse(event.data); } catch { return; }
    if (msg.type === 'hello') {
      state.boot = msg.boot || '';
    } else if (msg.type === 'snapshot') {
      state.rev = msg.rev;
      Object.assign(state, msg.state);
      saveTab(focusedStackId());
      syncPendingCursor();
      views.snapshot();
    } else if (msg.type === 'patch') {
      state.rev = msg.rev;
      for (const [key, body] of Object.entries(msg.sections || {})) state[key] = body;
      saveTab(focusedStackId());
      syncPendingCursor();
      views.patch(Object.keys(msg.sections || {}));
    } else if (msg.type === 'ack') {
      const wait = pendingAcks.get(msg.id);
      if (wait) {
        pendingAcks.delete(msg.id);
        wait(msg);
      }
      if (!msg.ok) {
        dropPendingCursors();
        toast(msg.error || 'Refused', 'error');
        views.refused();
      }
    } else if (msg.type === 'toast') {
      toast(msg.text, msg.level);
    } else if (msg.type === 'error') {
      toast(msg.text, 'error');
    }
  };

  ws.onclose = () => {
    socket = null;
    refuseWaiting();
    $('offline').hidden = false;
    reconnectTimer = setTimeout(reconnect, backoff);
    backoff = Math.min(backoff * 2, 15000);
  };

  ws.onerror = () => { try { ws.close(); } catch { /* handled */ } };
}

// A socket closed because this device is no longer signed in (its token
// expired, the PIN changed, every device was signed out) is refused however
// often it retries, so ask for the PIN instead. Otherwise try again.
async function reconnect() {
  reconnectTimer = 0;
  let hello = null;
  try {
    hello = await fetch('/api/hello', { headers: authHeaders() }).then((r) => r.json());
  } catch { /* SeventhSlide is unreachable: keep trying */ }
  // Signed in again meanwhile (which connected), or asked for the PIN: this
  // answer is stale.
  if (socket || !$('gate').hidden) return;
  if (hello && hello.needsPin && !hello.authed) {
    $('offline').hidden = true;
    showGate('Session expired. Enter the PIN again.');
    return;
  }
  connect();
}

async function acquireWakeLock() {
  if (!('wakeLock' in navigator)) return;
  try { await navigator.wakeLock.request('screen'); } catch { /* denied */ }
}

function showGate(message) {
  closeSocket();
  $('app').hidden = true;
  $('gate').hidden = false;
  const err = $('gateError');
  err.hidden = !message;
  err.textContent = message || '';
}

function showApp() {
  $('gate').hidden = true;
  $('app').hidden = false;
  connect();
  views.signedIn();
}

async function boot() {
  let hello;
  try {
    hello = await fetch('/api/hello', { headers: authHeaders() }).then((r) => r.json());
  } catch {
    showGate('Cannot reach SeventhSlide.');
    return;
  }
  if (!hello.needsPin || hello.authed) showApp();
  else showGate('');
}

/* Wire the PIN gate and start: ask for the PIN, or connect. ``handlers``
   hear what changed: ``snapshot()`` (all of it), ``patch(keys)`` (those
   sections), ``refused()`` (a command the server turned down) and
   ``signedIn()``. */
export function startConnection(handlers) {
  views = handlers;
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      acquireWakeLock();
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ type: 'resync' }));
      }
    }
  });

  $('gateForm').addEventListener('submit', async (e) => {
    e.preventDefault();
    const res = await fetch('/api/auth', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pin: $('gatePin').value }),
    });
    const body = await res.json().catch(() => ({}));
    if (!res.ok) {
      const wait = body.retryAfter;
      showGate(wait ? `Too many attempts. Try again in ${wait}s.`
                    : (body.error || 'Incorrect PIN.'));
      return;
    }
    state.token = body.token || '';
    localStorage.setItem('ss_token', state.token);
    $('gatePin').value = '';
    showApp();
  });

  boot();
}
