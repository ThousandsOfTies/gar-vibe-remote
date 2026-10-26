import test from 'node:test';
import assert from 'node:assert/strict';
import { CodexSessions } from '../src/codexSessions';

const config = {
  sessionScope: 'local' as const,
  sessionId: 'anchor',
  codexCwd: 'D:\\Work',
  chatQuestions: true,
  chatQuestionSources: ['isolated-physical-test']
};
const owner = 'isolated-roster-owner';
const row = (id: string, cwd = 'D:\\Work') => ({
  id,
  name: `作業 ${id}`,
  cwd,
  kind: 'codex',
  hostId: 'local',
  status: { type: 'active', activeFlags: [] }
});
const roster = (sessions = [row('a'), row('b')]) => ({ kind: 'codex-sessions', owner, sessions });
const question = (sessionId: string, id = 'same-native-question') => ({
  kind: 'chat-choice',
  sessionId,
  owner: `question-owner-${sessionId}-test`,
  questions: [
    {
      id,
      question: `質問 ${sessionId}`,
      options: [
        { id: 'yes', label: 'YES' },
        { id: 'no', label: 'NO' }
      ]
    }
  ],
  delivered: [],
  failed: []
});

test('identical native question ids in separate chats require both session and UI identities', () => {
  const hub = new CodexSessions(config);
  hub.sync(roster());
  hub.syncQuestions(question('a'));
  hub.syncQuestions(question('b'));
  const [a, b] = hub.state().sessions;
  assert.notEqual(a.ui!.id, b.ui!.id);
  assert.equal(hub.answer('b', a.ui!.id, 'yes', 'isolated-physical-test'), false);
  assert.equal(hub.answer(undefined, a.ui!.id, 'yes', 'isolated-physical-test'), false);
  assert.equal(hub.answer('a', a.ui!.id, 'yes', 'browser'), false);
  assert.equal(hub.answer('a', a.ui!.id, 'yes', 'isolated-physical-test'), true);
  assert.equal(hub.answer('a', a.ui!.id, 'yes', 'isolated-physical-test'), false);
  assert.equal(hub.syncQuestions(question('a')).answers.length, 1);
  assert.equal(hub.syncQuestions(question('b')).answers.length, 0);
  assert.equal(hub.answer('b', b.ui!.id, 'no', 'isolated-physical-test'), true);
});

test('removal, re-enrollment, roster order, independent owners and PC replies cannot revive an old UI id', () => {
  const hub = new CodexSessions(config);
  hub.sync(roster());
  hub.syncQuestions(question('a'));
  hub.syncQuestions(question('b'));
  const [a, b] = hub.state().sessions;
  assert.throws(() => hub.syncQuestions({ ...question('a'), owner: 'a-competing-question-owner' }));
  hub.sync(roster([row('b'), row('a')]));
  assert.deepEqual(
    hub.state().sessions.map((s) => s.session.id),
    ['b', 'a']
  );
  assert.equal(hub.state().sessions[0].ui!.id, b.ui!.id);
  hub.syncQuestions({ ...question('a'), questions: [] });
  assert.equal(hub.answer('a', a.ui!.id, 'yes', 'isolated-physical-test'), false);
  hub.sync(roster([row('b')]));
  assert.throws(() => hub.syncQuestions(question('a')));
  hub.sync(roster());
  hub.syncQuestions(question('a'));
  assert.notEqual(hub.state().sessions[0].ui!.id, a.ui!.id);
  assert.equal(hub.answer('b', b.ui!.id, 'no', 'isolated-physical-test'), true);
});

test('discovery is scoped to local Codex and invalid snapshots leave all prior sessions intact', () => {
  const hub = new CodexSessions({ ...config, sessionScope: 'project' });
  hub.sync(roster());
  for (const bad of [
    { ...row('other'), cwd: 'D:\\Elsewhere' },
    { ...row('other'), hostId: 'remote' },
    { ...row('other'), kind: 'chatgpt' },
    { ...row('other'), status: { type: 'unknown', activeFlags: [] } }
  ]) {
    assert.throws(() => hub.sync(roster([row('a'), bad])));
    assert.equal(hub.state().sessions.length, 2);
  }
  assert.throws(() => hub.sync(roster([row('a'), row('a')])));
  assert.throws(() => hub.sync(roster(Array.from({ length: 17 }, (_, i) => row(`s${i}`)))));
  assert.throws(() => hub.sync({ ...roster(), owner: 'competing-roster-owner' }));
  const manual = new CodexSessions({ ...config, sessionScope: 'manual', sessionIds: ['a'] });
  assert.throws(() => manual.sync(roster()));
  manual.sync(roster([row('a', 'D:\\Elsewhere')]));
  assert.equal(manual.state().sessions.length, 1);
});

test('question leases are independent; expired roster disables every answer until rediscovery', () => {
  let time = 1000;
  const hub = new CodexSessions(config, () => time);
  hub.sync(roster());
  hub.syncQuestions(question('a'));
  hub.syncQuestions(question('b'));
  const [a, b] = hub.state().sessions;
  time += 4000;
  hub.syncQuestions(question('b'));
  time += 1001;
  assert.equal(hub.answer('a', a.ui!.id, 'yes', 'isolated-physical-test'), false);
  assert.equal(hub.answer('b', b.ui!.id, 'yes', 'isolated-physical-test'), true);
  time += 10000;
  assert.equal(hub.state().sessions[0].ui!.id, 'session-collector-unavailable');
  assert.throws(() => hub.syncQuestions(question('b')));
  hub.sync({ ...roster(), owner: 'new-isolated-roster-owner' });
  hub.syncQuestions(question('b'));
  assert.notEqual(hub.state().sessions[1].ui!.id, b.ui!.id);
});

test('lifecycle and runtime status remain isolated even when sessions update concurrently', () => {
  const hub = new CodexSessions(config);
  hub.sync(roster());
  hub.ingest({ hook_event_name: 'PermissionRequest', session_id: 'a' });
  assert.equal(hub.state().sessions[0].ui!.state, 'waiting');
  assert.equal(hub.state().sessions[1].ui!.state, 'running');
  assert.equal(hub.ingest({ hook_event_name: 'Stop', session_id: 'unmonitored' }), false);
  hub.sync(
    roster([
      { ...row('a'), status: { type: 'idle', activeFlags: [] } },
      { ...row('b'), status: { type: 'active', activeFlags: ['waitingOnUserInput'] } }
    ])
  );
  assert.equal(hub.state().sessions[0].ui!.state, 'idle');
  assert.equal(hub.state().sessions[1].ui!.state, 'waiting');
});

test('sixteen large questions stay inside the 64 KiB wire budget without losing any option or identity', () => {
  const hub = new CodexSessions(config);
  hub.sync(roster(Array.from({ length: 16 }, (_, i) => row(`session-${i}`))));
  for (let i = 0; i < 16; i++) {
    const q = question(`session-${i}`);
    q.questions[0].question = '長'.repeat(2000);
    hub.syncQuestions(q);
  }
  const s = hub.state();
  assert.ok(Buffer.byteLength(JSON.stringify(s)) < 65536);
  assert.equal(s.sessions.length, 16);
  assert.equal(new Set(s.sessions.map((s) => s.session.id)).size, 16);
  assert.ok(s.sessions.every((s) => s.ui!.actions!.length === 2));
  const selected = JSON.stringify(s.sessions[0]);
  for (let i = 1; i < 16; i++) hub.syncQuestions({ ...question(`session-${i}`), questions: [] });
  assert.equal(JSON.stringify(hub.state().sessions[0]), selected);
});

test('escaped names and eight long escaped choice labels stay within the same fixed display budget', () => {
  const hub = new CodexSessions(config);
  hub.sync(
    roster(Array.from({ length: 16 }, (_, i) => ({ ...row(`s${i}`), name: '"'.repeat(512) })))
  );
  for (let i = 0; i < 16; i++) {
    const p = question(`s${i}`);
    p.questions[0].question = '長'.repeat(2000);
    p.questions[0].options = Array.from({ length: 8 }, (_, n) => ({
      id: `option-${n}-${'x'.repeat(68)}`,
      label: '"'.repeat(128)
    }));
    hub.syncQuestions(p);
  }
  const state = hub.state();
  assert.ok(Buffer.byteLength(JSON.stringify(state)) < 65536);
  assert.ok(state.sessions.every((s) => s.ui!.actions!.length === 8));
});
