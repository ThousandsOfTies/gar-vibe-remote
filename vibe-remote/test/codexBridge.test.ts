import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { WebSocket } from 'ws';
import { CodexStatusStore, parseCodexEvent } from '../src/codexStatus';
import { startCodexBridge } from '../src/codexBridge';
import type { StateMessage } from '../src/protocol';

const token = 'codex-bridge-test-private-token';
type BridgeMessage = {
  type: string;
  ok?: boolean;
  agent?: StateMessage['agent'];
  ui?: StateMessage['ui'];
  session?: StateMessage['session'];
};
function nextMessage(socket: WebSocket, match: (message: BridgeMessage) => boolean) {
  return new Promise<BridgeMessage>((resolve, reject) => {
    const timer = setTimeout(() => {
      socket.off('message', receive);
      reject(new Error('Timed out waiting for matching bridge message'));
    }, 3000);
    function receive(raw: unknown) {
      const message = JSON.parse(String(raw));
      if (!match(message)) return;
      clearTimeout(timer);
      socket.off('message', receive);
      resolve(message);
    }
    socket.on('message', receive);
  });
}
const event = (name: string, turn = 'turn-1', session = 'task-1') =>
  parseCodexEvent({
    hook_event_name: name,
    session_id: session,
    turn_id: turn,
    cwd: 'D:\\Example\\Demo',
    prompt: 'must not be transmitted',
    tool_input: { command: 'secret command' },
    last_assistant_message: 'private answer'
  });

test('Codex lifecycle distinguishes response end from task completion', () => {
  const store = new CodexStatusStore({ label: '時計の接続試験' });
  for (const [name, status, phase, detail] of [
    ['UserPromptSubmit', 'running', '実行中／ユーザー指示', 'ユーザー指示'],
    ['PreToolUse', 'running', '実行中／ツール実行前', 'ツール実行前'],
    ['PermissionRequest', 'waiting', '承認待ち', undefined],
    ['PostToolUse', 'running', '実行中／ツール実行後', 'ツール実行後'],
    ['Stop', 'idle', '応答終了', undefined],
    ['Interrupt', 'idle', '中断', undefined]
  ]) {
    assert.equal(store.ingest(event(name)), true);
    const state = store.state();
    assert.equal(state.agent?.status, status);
    assert.equal(state.ui?.stateLabel, undefined);
    assert.match(state.agent?.message ?? '', new RegExp(phase));
    assert.equal(state.ui?.message, detail ?? phase);
    assert.deepEqual(state.ui?.actions, []);
    assert.notEqual(state.agent?.status, 'done');
    assert.doesNotMatch(
      JSON.stringify(state),
      /secret command|private answer|must not be transmitted/
    );
  }
});

test('session identity is available before the first event and survives status changes', () => {
  const name = 'TargetをLilyGo T-Watch 2020に変更';
  const store = new CodexStatusStore({ sessionId: 'task-1', label: name });
  assert.deepEqual(store.state().session, { id: 'task-1', name });
  for (const eventName of ['UserPromptSubmit', 'PermissionRequest', 'Stop']) {
    store.ingest(event(eventName));
    assert.deepEqual(store.state().session, { id: 'task-1', name });
  }
});

test('selected task, late previous turns, compaction and out-of-order delivery are isolated', () => {
  let now = 1000;
  const store = new CodexStatusStore({ sessionId: 'task-1' }, () => now);
  assert.equal(store.ingest(event('UserPromptSubmit')), true);
  now = 1200;
  assert.equal(store.ingest(event('UserPromptSubmit', 'turn-2')), true);
  assert.equal(store.ingest(event('Stop', 'turn-1')), false);
  assert.equal(store.ingest(event('PermissionRequest', 'turn-2', 'other-task')), false);
  assert.equal(store.ingest({ ...event('SessionStart', 'turn-2'), source: 'compact' }), false);
  assert.equal(store.ingest({ ...event('Stop', 'turn-2'), emitted_at: 1100 }), false);
  assert.equal(store.state().agent?.status, 'running');
});

test('freshness expires without falsely reporting completion or renewing via reads', () => {
  let now = 1000;
  const store = new CodexStatusStore({ freshnessMs: 100 }, () => now);
  store.ingest(event('PermissionRequest'));
  now = 1099;
  assert.equal(store.state().agent?.status, 'waiting');
  now = 1100;
  assert.match(store.state().ui?.message ?? '', /状態更新なし/);
  assert.equal(store.state().agent?.updatedAt, 1000);
  store.ingest(event('PreToolUse'));
  assert.equal(store.state().ui?.message, 'ツール実行前');
  now = 1200;
  assert.equal(store.state().agent?.status, 'idle');
  assert.equal(store.state().ui?.stateLabel, undefined);
});

test('transport polling cannot turn a snapshot or test replay into automatic Codex updates', () => {
  let now = 1000;
  const store = new CodexStatusStore({ freshnessMs: 100 }, () => now);
  assert.deepEqual(store.state().monitor, {
    mode: 'waiting',
    fresh: false,
    observedAt: undefined,
    lastEvent: undefined
  });
  store.ingest(
    parseCodexEvent({
      hook_event_name: 'StatusSnapshot',
      session_id: 'task-1',
      status: { type: 'active' },
      delivery: 'hook'
    })
  );
  assert.equal(store.state().monitor?.mode, 'snapshot');
  store.ingest({ ...event('PermissionRequest'), delivery: 'test' });
  assert.equal(store.state().monitor?.mode, 'test');
  store.ingest({ ...event('PostToolUse'), delivery: 'hook' });
  assert.equal(store.state().monitor?.mode, 'hooks');
  now = 1100;
  const state = store.state();
  assert.equal(state.monitor?.fresh, false);
  assert.equal(state.monitor?.observedAt, 1000);
  assert.equal(state.monitor?.mode, 'hooks');
});

test('actual runtime snapshots carry waiting flags and reject unknown states', () => {
  const store = new CodexStatusStore({});
  for (const flag of ['waitingOnApproval', 'waitingOnUserInput']) {
    store.ingest(
      parseCodexEvent({
        hook_event_name: 'StatusSnapshot',
        session_id: 'task-1',
        status: { type: 'active', activeFlags: [flag] }
      })
    );
    assert.equal(store.state().agent?.status, 'waiting');
  }
  assert.throws(() =>
    parseCodexEvent({
      hook_event_name: 'StatusSnapshot',
      session_id: 'task-1',
      status: { type: 'invented' }
    })
  );
  assert.throws(() => parseCodexEvent({ hook_event_name: 'Stop', session_id: '../bad' }));
  assert.throws(() => parseCodexEvent({ hook_event_name: 'Unknown', session_id: 'task-1' }));
});

test('HTTP lifecycle input and authenticated WebSocket use the device protocol; actions stay disabled', async (t) => {
  const bridge = await startCodexBridge({ token, port: 0, sessionId: 'task-1' });
  t.after(() => bridge.close());
  const base = `http://127.0.0.1:${bridge.port}`;
  const post = (payload: unknown, credential = token) =>
    fetch(`${base}/events`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${credential}` },
      body: JSON.stringify(payload)
    });
  assert.equal((await post(event('UserPromptSubmit'), 'wrong')).status, 401);
  assert.equal((await post({ bad: true })).status, 400);
  assert.equal((await post({ ...event('Stop'), ignored: 'x'.repeat(17000) })).status, 413);
  const socket = new WebSocket(bridge.url);
  await once(socket, 'open');
  let messages = nextMessage(socket, (message) => message.type === 'ack');
  socket.send(JSON.stringify({ type: 'hello', token }));
  assert.equal((await messages).ok, true);
  messages = nextMessage(
    socket,
    (message) => message.type === 'state' && message.agent?.status === 'waiting'
  );
  assert.equal((await post(event('PermissionRequest'))).status, 200);
  const received = await messages;
  assert.equal(received.agent?.status, 'waiting');
  messages = nextMessage(socket, (message) => message.type === 'ack');
  socket.send(
    JSON.stringify({ type: 'uiAction', token, uiId: received.ui?.id, actionId: 'allow' })
  );
  assert.equal((await messages).ok, false);
  assert.equal(bridge.store.state().agent?.status, 'waiting');
  const other = await post(event('Stop', 'turn-1', 'other-task'));
  assert.equal((await other.json()).accepted, false);
});

test('unauthenticated WebSockets never receive state', async (t) => {
  const bridge = await startCodexBridge({ token, port: 0 });
  t.after(() => bridge.close());
  const socket = new WebSocket(bridge.url);
  const seen: string[] = [];
  socket.on('message', (raw) => seen.push(String(raw)));
  await once(socket, 'open');
  const closed = once(socket, 'close');
  socket.send(JSON.stringify({ type: 'hello', token: 'wrong-token' }));
  await closed;
  assert.equal(seen.length, 1);
  assert.equal(JSON.parse(seen[0]).ok, false);
  assert.ok(seen.every((raw) => !raw.includes('"state"')));
});

test('watch input test accepts one matching YES/NO and never changes Codex state', async (t) => {
  let now = 1000;
  const bridge = await startCodexBridge({ token, port: 0, now: () => now });
  t.after(() => bridge.close());
  bridge.store.ingest(event('UserPromptSubmit'));
  const base = `http://127.0.0.1:${bridge.port}`;
  const create = () =>
    fetch(`${base}/input-test`, {
      method: 'POST',
      headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
      body: '{}'
    });
  const result = async () =>
    (
      await (
        await fetch(`${base}/input-test`, {
          headers: { Authorization: `Bearer ${token}` }
        })
      ).json()
    ).test;
  assert.equal((await fetch(`${base}/input-test`)).status, 401);
  const created = await create();
  assert.equal(created.status, 201);
  const pending = (await created.json()).test;
  assert.equal((await create()).status, 409);
  const socket = new WebSocket(bridge.url);
  await once(socket, 'open');
  let response = nextMessage(socket, (m) => m.type === 'state');
  socket.send(JSON.stringify({ type: 'hello', token }));
  const shown = await response;
  assert.equal(shown.ui?.source, 'input-test');
  assert.deepEqual(
    shown.ui?.actions?.map((a) => a.id),
    ['yes', 'no']
  );
  const answer = async (uiId: string, actionId: string) => {
    const ack = nextMessage(socket, (m) => m.type === 'ack');
    socket.send(JSON.stringify({ type: 'uiAction', token, uiId, actionId, source: 'watch-test' }));
    return (await ack).ok;
  };
  assert.equal(await answer('stale-question', 'yes'), false);
  assert.equal(await answer(pending.id, 'allow'), false);
  assert.equal((await result()).status, 'pending');
  response = nextMessage(socket, (m) => m.type === 'state' && m.ui?.state === 'done');
  assert.equal(await answer(pending.id, 'yes'), true);
  assert.match((await response).ui?.message ?? '', /YES/);
  assert.equal(await answer(pending.id, 'no'), false);
  assert.equal((await result()).answer, 'yes');
  assert.equal(bridge.store.state().agent?.status, 'running');
  now += 8000;
  response = nextMessage(socket, (m) => m.type === 'state' && m.ui?.source === 'codex');
  socket.send(JSON.stringify({ type: 'ping', token }));
  assert.equal((await response).ui?.source, 'codex');
  const expired = (await (await create()).json()).test;
  now = expired.expiresAt;
  assert.equal(await answer(expired.id, 'no'), false);
  assert.equal((await result()).status, 'expired');
});

test('real questions require HTTP auth, an owner receipt and an authenticated device handshake', async (t) => {
  const bridge = await startCodexBridge({ token, port: 0, sessionId: 'task-1', watchInput: true });
  t.after(() => bridge.close());
  const base = `http://127.0.0.1:${bridge.port}`;
  const post = (route: string, payload: unknown, credential = token) =>
    fetch(base + route, {
      method: 'POST',
      headers: { Authorization: `Bearer ${credential}`, 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
  const question = { kind: 'question', sessionId: 'task-1', message: '続けますか？' };
  assert.equal((await post('/interactions', question, 'wrong')).status, 401);
  assert.equal((await post('/interactions', { ...question, sessionId: 'other' })).status, 400);
  const created = await post('/interactions', question);
  assert.equal(created.status, 201);
  const { interaction } = await created.json();
  assert.equal((await post('/input-test', {})).status, 409);
  assert.equal(
    (await post(`/interactions/${interaction.id}/poll`, { receipt: 'wrong' })).status,
    404
  );
  const socket = new WebSocket(bridge.url);
  await once(socket, 'open');
  let response = nextMessage(socket, (m) => m.type === 'ack');
  socket.send(JSON.stringify({ type: 'uiAction', token, uiId: interaction.id, actionId: 'yes' }));
  assert.equal((await response).ok, false);
  response = nextMessage(socket, (m) => m.type === 'state');
  socket.send(JSON.stringify({ type: 'hello', token }));
  const shown = await response;
  assert.equal(shown.ui?.source, 'codex-input');
  assert.deepEqual(shown.session, { id: 'task-1', name: 'Codex' });
  assert.doesNotMatch(JSON.stringify(shown), new RegExp(interaction.receipt));
  await post(`/interactions/${interaction.id}/cancel`, { receipt: interaction.receipt });
  response = nextMessage(socket, (m) => m.type === 'ack');
  socket.send(JSON.stringify({ type: 'uiAction', token, uiId: interaction.id, actionId: 'yes' }));
  assert.equal((await response).ok, false);
  const result = await (
    await post(`/interactions/${interaction.id}/poll`, { receipt: interaction.receipt })
  ).json();
  assert.equal(result.interaction.status, 'cancelled');
  assert.equal(result.interaction.answer, undefined);
});
