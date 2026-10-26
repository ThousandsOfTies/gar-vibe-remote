import test from 'node:test';
import assert from 'node:assert/strict';
import { CodexChatQuestions } from '../src/codexChatQuestions';
import { InteractionError } from '../src/codexInteraction';

const options = {
  chatQuestions: true,
  sessionId: 'selected-chat',
  chatQuestionSources: ['physical-test-device']
};
const first = {
  id: 'original-question-0',
  question: 'どれにしますか？',
  options: [
    { id: 'option-0', label: '選択肢１' },
    { id: 'option-1', label: '選択肢２' },
    { id: 'option-2', label: '選択肢３' }
  ]
};
const payload = (questions = [first]) => ({
  kind: 'chat-choice',
  sessionId: options.sessionId,
  owner: 'isolated-adapter-owner',
  questions,
  delivered: [],
  failed: []
});

test('ordinary choices require explicit configuration and never grant operation approval', () => {
  const disabled = new CodexChatQuestions({ sessionId: options.sessionId });
  assert.throws(
    () => disabled.sync(payload()),
    (e: unknown) => e instanceof InteractionError && e.status === 403
  );
  const q = new CodexChatQuestions(options);
  assert.throws(() => q.sync({ ...payload(), kind: 'approval' }));
  assert.throws(() => q.sync(payload([{ ...first, question: '\n'.repeat(1600) + '質問' }])));
  q.sync(payload());
  assert.equal(q.answer(q.ui()!.id, 'option-0', 'browser'), false);
  assert.equal(q.answer(q.ui()!.id, 'unknown-choice', 'physical-test-device'), false);
});

test('multiple native questions preserve identities; completed and stale choices cannot answer another question', () => {
  const q = new CodexChatQuestions(options);
  const second = { ...first, id: 'original-question-1', question: '次の質問' };
  const batch = payload([first, second]);
  q.sync(batch);
  const oldId = q.ui()!.id;
  assert.equal(q.answer(oldId, 'option-2', 'physical-test-device'), true);
  assert.deepEqual(q.sync(batch).answers, [
    { id: first.id, choiceId: 'option-2', source: 'physical-test-device' }
  ]);
  q.sync({ ...batch, delivered: [first.id] });
  assert.equal(q.ui()!.message, second.question);
  assert.notEqual(q.ui()!.id, oldId);
  assert.equal(q.answer(oldId, 'option-1', 'physical-test-device'), false);
  q.sync(payload([]));
  assert.equal(q.ui(), undefined);
});

test('invalid snapshots are atomic and an unavailable source revokes device input', () => {
  let now = 1000;
  const q = new CodexChatQuestions(options, () => now);
  q.sync(payload());
  const uiId = q.ui()!.id;
  q.answer(uiId, 'option-0', 'physical-test-device');
  assert.throws(() =>
    q.sync({ ...payload([{ ...first, question: 'reused id' }]), delivered: [first.id] })
  );
  assert.equal(q.sync(payload()).answers.length, 1);
  now += 5001;
  assert.equal(q.pending, false);
  assert.equal(q.ui(), undefined);
  assert.equal(q.answer(uiId, 'option-0', 'physical-test-device'), false);
  q.sync(payload());
  assert.notEqual(q.ui()!.id, uiId);
  q.sync({ ...payload([]), release: true });
  assert.doesNotThrow(() => q.sync({ ...payload(), owner: 'replacement-adapter-owner' }));
});

test('an unconfirmed reply is shown before the next question and cannot be selected again', () => {
  let now = 1000;
  const q = new CodexChatQuestions(options, () => now);
  const batch = payload([first, { ...first, id: 'second-question', question: '次の質問' }]);
  q.sync(batch);
  q.answer(q.ui()!.id, 'option-1', 'physical-test-device');
  const failed = { ...batch, failed: [first.id] };
  q.sync(failed);
  assert.equal(q.ui()!.message, 'PCで回答を確認してください');
  assert.deepEqual(q.ui()!.actions, []);
  now += 4000;
  q.sync(failed);
  now += 4000;
  q.sync(failed);
  assert.equal(q.ui()!.message, '次の質問');
});
