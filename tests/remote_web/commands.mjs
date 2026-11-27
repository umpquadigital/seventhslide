// Every remote command is answered for: each one in the
// protocol table (src/remote/remote_protocol.cpp) is sent by the bundled
// client or held to what it does by a bridge test
// (tests/test_session_remote_bridge.cpp), and docs/REMOTE_PROTOCOL.md marks
// exactly those the client does not send. lint.mjs runs it.
//
//     node commands.mjs

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const top = path.resolve(here, '..', '..');
const read = (rel) => fs.readFileSync(path.join(top, rel), 'utf8');

/** The table's commands: enum name to wire name. */
function protocolTable() {
  const table = new Map();
  for (const m of read('src/remote/remote_protocol.cpp').matchAll(/\{C::(\w+),\s*"(\w+)"/g)) {
    table.set(m[1], m[2]);
  }
  return table;
}

/** Wire names the client says anywhere as a string: a command it sends,
 *  directly or through a variable. */
function clientNames() {
  const dir = 'src/remote/web/js';
  const names = new Set();
  for (const file of fs.readdirSync(path.join(top, dir))) {
    for (const m of read(path.join(dir, file)).matchAll(/'(\w+)'/g)) names.add(m[1]);
  }
  return names;
}

function testedCommands() {
  return new Set([...read('tests/test_session_remote_bridge.cpp')
    .matchAll(/Command::(\w+)/g)].map((m) => m[1]));
}

/** Commands the protocol document marks as not sent by the bundled client. */
function markedUnsent() {
  return new Set([...read('docs/REMOTE_PROTOCOL.md')
    .matchAll(/^\| `(\w+)` \*\(not sent by the bundled client\)\*/gm)].map((m) => m[1]));
}

export function checkCommands() {
  const table = protocolTable();
  const sent = clientNames();
  const tested = testedCommands();
  const marked = markedUnsent();
  const problems = [];
  if (!table.size) problems.push('no commands found in remote_protocol.cpp');
  for (const [command, wire] of table) {
    const isSent = sent.has(wire);
    if (!isSent && !tested.has(command)) {
      problems.push(`${wire}: neither sent by the client nor tested in test_session_remote_bridge.cpp`);
    }
    if (isSent && marked.has(wire)) {
      problems.push(`${wire}: marked "not sent by the bundled client" in REMOTE_PROTOCOL.md, but it is`);
    }
    if (!isSent && !marked.has(wire)) {
      problems.push(`${wire}: not sent by the client; mark it so in REMOTE_PROTOCOL.md`);
    }
  }
  return problems;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const problems = checkCommands();
  for (const p of problems) console.error(p);
  process.exit(problems.length ? 1 : 0);
}
