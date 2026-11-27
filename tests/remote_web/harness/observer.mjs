// A second client of the remote, speaking the protocol directly
// (docs/REMOTE_PROTOCOL.md): what the server holds, read apart from what the
// page shows. It is a device of its own, so its sections describe its own
// stack tab.

const WAIT_TIMEOUT_MS = 5_000;

export class Observer {
  constructor(app, token, socket) {
    this.app = app;
    this.token = token;
    this.socket = socket;
    /** Every section, as the last snapshot and patches left them. */
    this.state = {};
    this.boot = '';
    this.seq = 0;
    this.acks = new Map();
    this.listeners = new Set();
    socket.addEventListener('message', (event) => this.receive(JSON.parse(event.data)));
  }

  /** Sign in with the library's PIN and wait for the first snapshot. */
  static async connect(app) {
    const res = await fetch(`${app.url}/api/auth`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ pin: app.fixture.pin }),
    });
    if (!res.ok) throw new Error(`Observer sign-in failed: ${res.status}`);
    const { token } = await res.json();
    const socket = new WebSocket(`ws://127.0.0.1:${app.port}/ws?token=${encodeURIComponent(token)}`);
    const observer = new Observer(app, token, socket);
    await observer.waitFor((s) => s.session !== undefined, 'the first snapshot');
    return observer;
  }

  receive(msg) {
    if (msg.type === 'hello') {
      this.boot = msg.boot;
    } else if (msg.type === 'snapshot') {
      this.state = { ...msg.state };
    } else if (msg.type === 'patch') {
      Object.assign(this.state, msg.sections);
    } else if (msg.type === 'ack') {
      const resolve = this.acks.get(msg.id);
      this.acks.delete(msg.id);
      if (resolve) resolve(msg);
    }
    for (const listener of this.listeners) listener();
  }

  get session() { return this.state.session || {}; }
  get service() { return this.state.service || { items: [] }; }
  get stacks() { return this.state.stacks || {}; }
  get outputs() { return (this.state.outputs && this.state.outputs.list) || []; }
  get lines() { return this.state.lines || { rows: [] }; }
  get images() { return this.state.images || {}; }
  get advance() { return (this.state.advance && this.state.advance.byStack) || {}; }
  get preview() { return this.state.preview || {}; }

  /** Send a command; answers the ack (``ok``, ``error``, ``data``). */
  send(name, args = {}) {
    const id = `obs${++this.seq}`;
    return new Promise((resolve) => {
      this.acks.set(id, resolve);
      this.socket.send(JSON.stringify({ type: 'cmd', id, name, args }));
    });
  }

  /** Send a command that has to succeed. */
  async run(name, args = {}) {
    const ack = await this.send(name, args);
    if (!ack.ok) throw new Error(`${name} was refused: ${ack.error}`);
    return ack;
  }

  /** Wait until ``predicate(state)`` holds; ``what`` names it in the failure. */
  waitFor(predicate, what, timeout = WAIT_TIMEOUT_MS) {
    const holds = () => {
      try { return !!predicate(this.state); } catch { return false; }
    };
    if (holds()) return Promise.resolve(this.state);
    return new Promise((resolve, reject) => {
      const done = () => {
        clearTimeout(timer);
        this.listeners.delete(check);
      };
      const check = () => {
        if (holds()) {
          done();
          resolve(this.state);
        }
      };
      const timer = setTimeout(() => {
        done();
        reject(new Error(`Timed out after ${timeout} ms waiting for ${what}; session: `
          + JSON.stringify(this.state.session)));
      }, timeout);
      this.listeners.add(check);
    });
  }

  close() {
    this.socket.close();
  }
}
