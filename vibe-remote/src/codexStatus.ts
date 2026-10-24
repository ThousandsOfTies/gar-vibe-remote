import * as path from 'node:path';
import type { AgentRuntimeStatus, StateMessage } from './protocol';

/** Only lifecycle metadata is retained; prompts, commands and answers are not forwarded. */
export interface CodexEvent {
  hook_event_name: string;
  session_id: string;
  turn_id?: string;
  cwd?: string;
  tool_name?: string;
  source?: string;
  emitted_at?: number;
  delivery?: 'hook' | 'test';
  status?: { type: string; activeFlags?: string[] };
}

const events = new Set([
  'SessionStart',
  'SessionEnd',
  'UserPromptSubmit',
  'PreToolUse',
  'PermissionRequest',
  'PostToolUse',
  'Stop',
  'Interrupt',
  'StatusSnapshot'
]);
const clean = (value: unknown, max = 120): string =>
  typeof value === 'string' ? value.replace(/\p{Cc}/gu, ' ').slice(0, max) : '';

export function parseCodexEvent(input: unknown): CodexEvent {
  if (!input || typeof input !== 'object' || Array.isArray(input)) {
    throw new Error('Codex event must be an object');
  }
  const value = input as Record<string, unknown>;
  const name = clean(value.hook_event_name, 40);
  const session = clean(value.session_id, 128);
  if (!events.has(name) || !/^[a-zA-Z0-9_-]{1,128}$/.test(session)) {
    throw new Error('Unsupported event or invalid session_id');
  }
  const result: CodexEvent = { hook_event_name: name, session_id: session };
  if (value.delivery === 'hook' || value.delivery === 'test') result.delivery = value.delivery;
  for (const key of ['turn_id', 'cwd', 'tool_name', 'source'] as const) {
    if (typeof value[key] === 'string') result[key] = clean(value[key]);
  }
  if (typeof value.emitted_at === 'number' && Number.isFinite(value.emitted_at)) {
    result.emitted_at = value.emitted_at;
  }
  if (name === 'StatusSnapshot') {
    const status = value.status as Record<string, unknown> | undefined;
    if (!status || !['active', 'idle', 'notLoaded', 'systemError'].includes(String(status.type))) {
      throw new Error('Invalid Codex runtime status');
    }
    result.status = {
      type: String(status.type),
      activeFlags: Array.isArray(status.activeFlags)
        ? status.activeFlags.filter((flag): flag is string => typeof flag === 'string').slice(0, 8)
        : []
    };
  }
  return result;
}

interface Observation {
  session: string;
  turn?: string;
  task: string;
  status: AgentRuntimeStatus;
  phase: string;
  updatedAt: number;
  emittedAt: number;
  lastEvent: string;
  mode: 'hooks' | 'test' | 'snapshot';
}

export class CodexStatusStore {
  private observation?: Observation;
  constructor(
    readonly options: { sessionId?: string; label?: string; freshnessMs?: number },
    private readonly now = Date.now
  ) {}

  ingest(event: CodexEvent): boolean {
    if (this.options.sessionId && event.session_id !== this.options.sessionId) return false;
    const current = this.observation;
    const now = this.now();
    const emittedAt = Math.min(event.emitted_at ?? now, now);
    if (current?.session === event.session_id) {
      if (emittedAt < current.emittedAt) return false;
      // A late completion from the previous turn must not overwrite a new turn.
      if (
        event.hook_event_name !== 'UserPromptSubmit' &&
        event.turn_id &&
        current.turn &&
        event.turn_id !== current.turn
      )
        return false;
    }
    if (event.hook_event_name === 'SessionStart' && event.source === 'compact' && current)
      return false;
    const observation: Observation = {
      session: event.session_id,
      turn: event.turn_id || (current?.session === event.session_id ? current.turn : undefined),
      task: clean(this.options.label) || path.win32.basename(event.cwd || '') || 'Codex',
      status: 'running',
      phase: '実行中',
      updatedAt: now,
      emittedAt,
      lastEvent: event.hook_event_name,
      mode:
        event.hook_event_name === 'StatusSnapshot'
          ? 'snapshot'
          : event.delivery === 'hook'
            ? 'hooks'
            : 'test'
    };
    switch (event.hook_event_name) {
      case 'UserPromptSubmit':
        observation.phase = '実行中／ユーザー指示';
        break;
      case 'PreToolUse':
        observation.phase = '実行中／ツール実行前';
        break;
      case 'PostToolUse':
        observation.phase = '実行中／ツール実行後';
        break;
      case 'SessionStart':
        observation.status = 'idle';
        observation.phase = 'セッション開始';
        break;
      case 'SessionEnd':
        observation.status = 'idle';
        observation.phase = 'セッション終了';
        break;
      case 'PermissionRequest':
        observation.status = 'waiting';
        observation.phase = '承認待ち';
        break;
      case 'Stop':
        observation.status = 'idle';
        observation.phase = '応答終了';
        break;
      case 'Interrupt':
        observation.status = 'idle';
        observation.phase = '中断';
        break;
      case 'StatusSnapshot': {
        const status = event.status!;
        if (status.type === 'systemError') {
          observation.status = 'failed';
          observation.phase = 'エラー';
        } else if (status.type === 'notLoaded') {
          observation.status = 'idle';
          observation.phase = 'タスク未接続';
        } else if (status.type === 'idle') {
          observation.status = 'idle';
          observation.phase = '応答待機';
        } else if (status.activeFlags?.includes('waitingOnApproval')) {
          observation.status = 'waiting';
          observation.phase = '承認待ち';
        } else if (status.activeFlags?.includes('waitingOnUserInput')) {
          observation.status = 'waiting';
          observation.phase = '回答待ち';
        }
        break;
      }
    }
    this.observation = observation;
    return true;
  }

  state(): StateMessage {
    const now = this.now();
    const observed = this.observation;
    const stale = !!observed && now - observed.updatedAt >= (this.options.freshnessMs ?? 90_000);
    const status = stale ? 'idle' : (observed?.status ?? 'idle');
    const phase = stale ? '状態更新なし' : (observed?.phase ?? 'Codex未接続');
    const message = !observed
      ? 'Codexイベント待ち'
      : stale
        ? '状態更新なし・PCを確認'
        : `${phase} · ${observed.task}`;
    // The selector already displays the session name. Keep the body to the
    // event detail, while preserving the complete description in agent/log data.
    const runningDetail =
      status === 'running' && phase.startsWith('実行中／')
        ? phase.slice('実行中／'.length)
        : undefined;
    return {
      type: 'state',
      session:
        observed || this.options.sessionId
          ? {
              id: observed?.session ?? this.options.sessionId!,
              name: clean(this.options.label) || observed?.task || 'Codex'
            }
          : undefined,
      monitor: {
        mode: observed?.mode ?? 'waiting',
        fresh: !!observed && !stale,
        observedAt: observed?.updatedAt,
        lastEvent: observed?.lastEvent
      },
      chat: status === 'running' ? 'working' : status === 'waiting' ? 'maybeWaiting' : 'idle',
      agent: {
        source: 'codex',
        status,
        message,
        updatedAt: observed?.updatedAt ?? now,
        expiresAt: observed ? observed.updatedAt + (this.options.freshnessMs ?? 90_000) : undefined
      },
      ui: {
        id: `codex-${observed?.session ?? 'disconnected'}`,
        title: '',
        state: status,
        message: runningDetail ?? (observed && !stale ? phase : message),
        actions: [],
        source: 'codex',
        updatedAt: observed?.updatedAt ?? now
      },
      activity: {
        errors: 0,
        warnings: 0,
        debugging: false,
        taskRunning: status === 'running',
        focused: false
      },
      ts: now
    };
  }

  summary() {
    return {
      sessionId: this.observation?.session,
      turnId: this.observation?.turn,
      lastEvent: this.observation?.lastEvent,
      phase: this.observation ? this.state().agent?.message : 'Codex未接続'
    };
  }
}
