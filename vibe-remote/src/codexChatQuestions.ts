import { randomBytes } from 'node:crypto';
import { InteractionError } from './codexInteraction';
import type { DeviceUiSpec } from './protocol';

interface Question {
  id: string;
  uiId: string;
  question: string;
  options: { id: string; label: string }[];
  status: 'pending' | 'selected' | 'delivered' | 'failed';
  choiceId?: string;
  source?: string;
  updatedAt: number;
}

/** Ordinary chat choices. This channel never creates an execution approval. */
export class CodexChatQuestions {
  private questions = new Map<string, Question>();
  private owner?: string;
  private leaseUntil = 0;

  constructor(
    private readonly options: {
      chatQuestions?: boolean;
      sessionId?: string;
      chatQuestionSources?: string[];
    },
    private readonly now = Date.now
  ) {}

  private refresh() {
    if (this.now() >= this.leaseUntil) {
      this.questions.clear();
      this.owner = undefined;
    }
  }

  get pending() {
    this.refresh();
    return [...this.questions.values()].some((q) => ['pending', 'selected'].includes(q.status));
  }

  sync(payload: unknown) {
    this.refresh();
    if (!this.options.chatQuestions || !this.options.sessionId)
      throw new InteractionError(403, 'Chat questions require an enabled, selected session');
    const p = payload as Record<string, unknown> | null;
    if (
      !p ||
      p.kind !== 'chat-choice' ||
      p.sessionId !== this.options.sessionId ||
      typeof p.owner !== 'string' ||
      !/^[\w-]{16,128}$/.test(p.owner) ||
      !Array.isArray(p.questions) ||
      p.questions.length > 24 ||
      (p.release !== undefined && typeof p.release !== 'boolean') ||
      (p.release === true && p.questions.length !== 0) ||
      !Array.isArray(p.delivered) ||
      !Array.isArray(p.failed) ||
      [...p.delivered, ...p.failed].some((id) => typeof id !== 'string')
    )
      throw new InteractionError(400, 'Invalid chat question snapshot');
    if (this.owner && this.owner !== p.owner)
      throw new InteractionError(409, 'Another chat question adapter is connected');
    const next = new Map<string, Question>();
    for (const input of p.questions) {
      const q = input as Record<string, unknown>;
      if (
        !q ||
        typeof q.id !== 'string' ||
        !q.id ||
        q.id.length > 256 ||
        typeof q.question !== 'string' ||
        !q.question.trim() ||
        q.question.length > 2000 ||
        q.question.split('\n').length > 200 ||
        /\p{Cc}/u.test(q.question.replace(/[\n\t]/g, '')) ||
        !Array.isArray(q.options) ||
        q.options.length < 2 ||
        q.options.length > 8 ||
        next.has(q.id)
      )
        throw new InteractionError(400, 'Invalid chat question');
      const ids = new Set<string>();
      for (const option of q.options) {
        if (
          !option ||
          typeof option.id !== 'string' ||
          !/^[\w-]{1,80}$/.test(option.id) ||
          ids.has(option.id) ||
          typeof option.label !== 'string' ||
          !option.label.trim() ||
          Buffer.byteLength(option.label) > 128 ||
          /[\p{Cc}]/u.test(option.label)
        )
          throw new InteractionError(400, 'Invalid chat choice');
        ids.add(option.id);
      }
      const existing = this.questions.get(q.id);
      if (
        existing &&
        (existing.question !== q.question ||
          JSON.stringify(existing.options) !== JSON.stringify(q.options))
      )
        throw new InteractionError(409, 'A question id cannot be reused with different content');
      const value: Question = existing ?? {
        id: q.id,
        uiId: `chat-choice-${randomBytes(16).toString('hex')}`,
        question: q.question,
        options: q.options,
        status: 'pending',
        updatedAt: this.now()
      };
      // Clone before validation finishes: a rejected snapshot must change nothing.
      next.set(q.id, { ...value });
    }
    for (const [status, values] of [
      ['delivered', p.delivered],
      ['failed', p.failed]
    ] as const) {
      for (const id of values as string[]) {
        const q = next.get(id);
        if (q && ['pending', 'selected'].includes(q.status)) {
          q.status = status;
          q.updatedAt = this.now();
        }
      }
    }
    this.owner = p.release === true ? undefined : p.owner;
    this.questions = next;
    this.leaseUntil = p.release === true ? 0 : this.now() + 5000;
    return {
      answers: [...next.values()]
        .filter((q) => q.status === 'selected')
        .map((q) => ({
          id: q.id,
          choiceId: q.choiceId,
          source: q.source
        }))
    };
  }

  answer(uiId: unknown, choiceId: unknown, source: unknown) {
    this.refresh();
    const q = this.current();
    if (
      !q ||
      q.status !== 'pending' ||
      q.uiId !== uiId ||
      typeof source !== 'string' ||
      !Array.isArray(this.options.chatQuestionSources) ||
      !this.options.chatQuestionSources.includes(source) ||
      !q.options.some((option) => option.id === choiceId)
    )
      return false;
    q.status = 'selected';
    q.choiceId = choiceId as string;
    q.source = source;
    q.updatedAt = this.now();
    return true;
  }

  private current() {
    const questions = [...this.questions.values()];
    return (
      questions.find((q) => q.status === 'failed' && this.now() - q.updatedAt < 8000) ??
      questions.find((q) => ['pending', 'selected'].includes(q.status)) ??
      questions.find((q) => this.now() - q.updatedAt < 8000)
    );
  }

  ui(): DeviceUiSpec | undefined {
    this.refresh();
    const q = this.current();
    if (!q) return;
    const label = q.options.find((option) => option.id === q.choiceId)?.label;
    return {
      id: q.uiId,
      title: '',
      source: 'chat-choice',
      mode: 'menu',
      state: q.status === 'delivered' ? 'done' : q.status === 'failed' ? 'idle' : 'waiting',
      message:
        q.status === 'pending'
          ? q.question
          : q.status === 'selected'
            ? '回答をPCへ送信中'
            : q.status === 'delivered'
              ? label
                ? `${label}を送信しました`
                : '回答を送信しました'
              : 'PCで回答を確認してください',
      actions: q.status === 'pending' ? q.options : [],
      updatedAt: q.updatedAt
    };
  }
}
