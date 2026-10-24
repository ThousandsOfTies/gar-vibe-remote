import test from 'node:test';
import assert from 'node:assert/strict';
import { CodexInteractions } from '../src/codexInteraction';

const request = {
  kind: 'approval',
  sessionId: 'task-1',
  turnId: 'turn-1',
  message: 'Bash | D:/demo | echo ok'
};
const observed = { sessionId: 'task-1', turnId: 'turn-1' };

test('only an enabled selected chat and current turn can request input', () => {
  assert.throws(() => new CodexInteractions({}).create(request, observed));
  const inputs = new CodexInteractions({ watchInput: true, sessionId: 'task-1' });
  for (const payload of [
    { ...request, sessionId: 'other' },
    { ...request, turnId: 'old' },
    { ...request, turnId: undefined },
    { ...request, message: '' },
    { ...request, message: 'x'.repeat(1001) },
    { ...request, message: '\u202ehidden' },
    { ...request, timeoutMs: 120001 }
  ])
    assert.throws(() => inputs.create(payload, observed));
  inputs.create(request, observed);
  assert.throws(() => inputs.create(request, observed), /pending/);
});

test('answers require the current unguessable id and can be given only once', () => {
  const inputs = new CodexInteractions({ watchInput: true, sessionId: 'task-1' });
  const first = inputs.create(request, observed);
  assert.equal(inputs.ui()?.mode, 'direct');
  assert.equal(inputs.ui()?.actions?.[0].button, 'A-hold');
  assert.doesNotMatch(JSON.stringify(inputs.ui()), new RegExp(first.receipt));
  assert.throws(() => inputs.poll(first.id, 'wrong'));
  assert.equal(inputs.answer('old', 'yes', 'device'), false);
  assert.equal(inputs.answer(first.id, 'allow-all', 'device'), false);
  assert.equal(inputs.answer(first.id, 'yes', 'device'), true);
  assert.equal(inputs.answer(first.id, 'no', 'device'), false);
  assert.equal(inputs.poll(first.id, first.receipt).answer, 'yes');
  assert.equal(inputs.ui()?.message, 'YESを送信しました');
  assert.equal(inputs.ui()?.title, '');
  assert.equal(inputs.ui()?.stateLabel, '');
  const next = inputs.create({ ...request, kind: 'question' }, observed);
  assert.notEqual(first.id, next.id);
  assert.equal(inputs.ui()?.mode, 'menu');
  assert.equal(inputs.answer(first.id, 'yes', 'device'), false);
});

test('timeout, abandoned caller lease and cancellation never become answers', () => {
  let now = 1000;
  const inputs = new CodexInteractions({ watchInput: true, sessionId: 'task-1' }, () => now);
  let current = inputs.create(request, observed);
  now += 5000;
  assert.equal(inputs.answer(current.id, 'yes', 'watch'), false);
  assert.equal(inputs.poll(current.id, current.receipt).status, 'expired');
  assert.equal(inputs.poll(current.id, current.receipt).answer, undefined);
  current = inputs.create({ ...request, timeoutMs: 1000 }, observed);
  now += 1000;
  assert.equal(inputs.answer(current.id, 'no', 'watch'), false);
  current = inputs.create(request, observed);
  assert.equal(inputs.poll(current.id, current.receipt, true).status, 'cancelled');
  assert.equal(inputs.answer(current.id, 'yes', 'watch'), false);
  now += 8000;
  assert.equal(inputs.ui(), undefined);
});

test('active polling renews only the caller lease, never the question deadline', () => {
  let now = 1000;
  const inputs = new CodexInteractions({ watchInput: true, sessionId: 'task-1' }, () => now);
  const current = inputs.create({ ...request, timeoutMs: 10000 }, observed);
  for (now = 5000; now < 11000; now += 1000)
    assert.equal(inputs.poll(current.id, current.receipt).status, 'pending');
  assert.equal(inputs.poll(current.id, current.receipt).status, 'expired');
});

test('interrupt, session end, completed turn and a new turn invalidate pending input', () => {
  for (const event of ['Interrupt', 'SessionEnd', 'Stop', 'UserPromptSubmit']) {
    const inputs = new CodexInteractions({ watchInput: true, sessionId: 'task-1' });
    const current = inputs.create(request, observed);
    inputs.observe({ hook_event_name: event, session_id: 'other', turn_id: 'turn-2' });
    assert.equal(inputs.pending, true);
    inputs.observe({ hook_event_name: event, session_id: 'task-1', turn_id: 'turn-2' });
    assert.equal(inputs.poll(current.id, current.receipt).status, 'cancelled');
    assert.equal(inputs.answer(current.id, 'yes', 'device'), false);
  }
});

test('an interrupt between the tap and delivery cancels the answer before Codex can receive it', () => {
  const inputs = new CodexInteractions({ watchInput: true, sessionId: 'task-1' });
  const current = inputs.create(request, observed);
  inputs.answer(current.id, 'yes', 'device');
  inputs.observe({ hook_event_name: 'Interrupt', session_id: 'task-1', turn_id: 'turn-1' });
  const result = inputs.poll(current.id, current.receipt);
  assert.equal(result.status, 'cancelled');
  assert.equal(result.answer, undefined);
});

test('the caller can cancel a tapped answer before it has received that answer', () => {
  const inputs = new CodexInteractions({ watchInput: true, sessionId: 'task-1' });
  const current = inputs.create(request, observed);
  inputs.answer(current.id, 'yes', 'device');
  const result = inputs.poll(current.id, current.receipt, true);
  assert.equal(result.status, 'cancelled');
  assert.equal(result.answer, undefined);
});
