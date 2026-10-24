import { randomBytes } from 'node:crypto';
import type { DeviceUiSpec } from './protocol';

type Status = 'pending' | 'answered' | 'expired' | 'cancelled';
interface Interaction {
  id: string;
  receipt: string;
  kind: 'question' | 'approval';
  sessionId: string;
  turnId?: string;
  message: string;
  createdAt: number;
  expiresAt: number;
  leaseUntil: number;
  status: Status;
  answer?: 'yes' | 'no';
  source?: string;
  finishedAt?: number;
  delivered?: boolean;
}

export class InteractionError extends Error {
  constructor(
    readonly status: number,
    message: string
  ) {
    super(message);
  }
}

/** The HTTP caller owns the wait; devices can only answer its current random UI id. */
export class CodexInteractions {
  private requests = new Map<string, Interaction>();
  private current?: Interaction;

  constructor(
    private readonly options: { watchInput?: boolean; sessionId?: string },
    private readonly now = Date.now
  ) {}

  private refresh() {
    const now = this.now();
    for (const [id, request] of this.requests) {
      if (request.status === 'pending' && now >= Math.min(request.expiresAt, request.leaseUntil)) {
        request.status = 'expired';
        request.finishedAt = now;
      }
      if (request.finishedAt !== undefined && now - request.finishedAt >= 60000)
        this.requests.delete(id);
    }
  }

  get pending() {
    this.refresh();
    return this.current?.status === 'pending';
  }

  create(payload: unknown, observed: { sessionId?: string; turnId?: string }) {
    this.refresh();
    if (!this.options.watchInput || !this.options.sessionId)
      throw new InteractionError(403, 'Watch input requires an enabled, selected Codex session');
    if (!payload || typeof payload !== 'object' || Array.isArray(payload))
      throw new InteractionError(400, 'Invalid interaction');
    const value = payload as Record<string, unknown>;
    if (
      !['question', 'approval'].includes(String(value.kind)) ||
      value.sessionId !== this.options.sessionId ||
      (observed.sessionId && value.sessionId !== observed.sessionId) ||
      (value.turnId !== undefined &&
        (typeof value.turnId !== 'string' || !/^[\w-]{1,128}$/.test(value.turnId))) ||
      (observed.turnId && value.turnId && value.turnId !== observed.turnId) ||
      typeof value.message !== 'string' ||
      !value.message.trim() ||
      value.message.length > 1000 ||
      /[\p{Cc}\p{Cf}]/u.test(value.message) ||
      (value.kind === 'approval' && !value.turnId) ||
      (value.timeoutMs !== undefined &&
        (!Number.isInteger(value.timeoutMs) ||
          Number(value.timeoutMs) < 1000 ||
          Number(value.timeoutMs) > 120000))
    )
      throw new InteractionError(400, 'Invalid interaction or wrong Codex session/turn');
    if (this.pending) throw new InteractionError(409, 'Another watch question is pending');
    // Bound both retention and memory, including rapid completed requests.
    while (this.requests.size >= 32) this.requests.delete(this.requests.keys().next().value!);
    const request: Interaction = {
      id: `codex-input-${randomBytes(16).toString('hex')}`,
      receipt: randomBytes(24).toString('hex'),
      kind: value.kind as Interaction['kind'],
      sessionId: value.sessionId as string,
      turnId: value.turnId as string | undefined,
      message: value.message,
      createdAt: this.now(),
      expiresAt: this.now() + Number(value.timeoutMs ?? 60000),
      leaseUntil: this.now() + 5000,
      status: 'pending'
    };
    this.current = request;
    this.requests.set(request.id, request);
    return { id: request.id, receipt: request.receipt, expiresAt: request.expiresAt };
  }

  poll(id: string, receipt: unknown, cancel = false) {
    this.refresh();
    const request = this.requests.get(id);
    if (!request || typeof receipt !== 'string' || receipt !== request.receipt)
      throw new InteractionError(404, 'Unknown interaction');
    if (
      cancel &&
      (request.status === 'pending' || (request.status === 'answered' && !request.delivered))
    ) {
      request.status = 'cancelled';
      request.answer = undefined;
      request.source = undefined;
      request.finishedAt = this.now();
    } else if (request.status === 'pending') request.leaseUntil = this.now() + 5000;
    if (!cancel && request.status === 'answered') request.delivered = true;
    return {
      id: request.id,
      kind: request.kind,
      sessionId: request.sessionId,
      turnId: request.turnId,
      status: request.status,
      answer: request.answer,
      source: request.source,
      expiresAt: request.expiresAt
    };
  }

  answer(id: unknown, action: unknown, source: unknown) {
    this.refresh();
    const request = this.current;
    if (
      !request ||
      request.status !== 'pending' ||
      id !== request.id ||
      (action !== 'yes' && action !== 'no')
    )
      return false;
    request.status = 'answered';
    request.answer = action;
    request.source =
      typeof source === 'string' ? source.replace(/\p{Cc}/gu, ' ').slice(0, 80) : 'watch';
    request.finishedAt = this.now();
    return true;
  }

  observe(event: { hook_event_name: string; session_id: string; turn_id?: string }) {
    const request = this.current;
    if (
      !request ||
      !['pending', 'answered'].includes(request.status) ||
      request.delivered ||
      event.session_id !== request.sessionId
    )
      return;
    if (
      ['Stop', 'Interrupt', 'SessionEnd'].includes(event.hook_event_name) ||
      (event.hook_event_name === 'UserPromptSubmit' &&
        (!request.turnId || event.turn_id !== request.turnId))
    ) {
      request.status = 'cancelled';
      request.answer = undefined;
      request.source = undefined;
      request.finishedAt = this.now();
    }
  }

  ui(): DeviceUiSpec | undefined {
    this.refresh();
    const request = this.current;
    if (!request || (request.finishedAt !== undefined && this.now() - request.finishedAt >= 8000))
      return;
    const pending = request.status === 'pending';
    return {
      id: request.id,
      title: '',
      stateLabel: pending ? (request.kind === 'approval' ? '実行の承認' : '質問への回答') : '',
      source: 'codex-input',
      state: pending ? 'waiting' : request.answer ? 'done' : 'idle',
      mode: request.kind === 'approval' ? 'direct' : 'menu',
      message: pending
        ? request.message
        : request.answer
          ? `${request.answer.toUpperCase()}を送信しました`
          : 'PCで確認してください',
      actions: pending
        ? [
            { id: 'yes', label: 'YES', button: request.kind === 'approval' ? 'A-hold' : 'A' },
            { id: 'no', label: 'NO', button: request.kind === 'approval' ? 'P' : 'B' }
          ]
        : [],
      updatedAt: request.finishedAt ?? request.createdAt,
      expiresAt: request.expiresAt
    };
  }
}
