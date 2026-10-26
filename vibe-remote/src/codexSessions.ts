import * as path from 'node:path';
import { CodexChatQuestions } from './codexChatQuestions';
import { CodexStatusStore, type CodexEvent } from './codexStatus';
import { InteractionError } from './codexInteraction';
import type { SessionsMessage } from './protocol';

export interface SessionOptions {
  sessionId?: string;
  label?: string;
  codexCwd?: string;
  sessionScope?: 'single' | 'project' | 'local' | 'manual';
  sessionIds?: string[];
  chatQuestions?: boolean;
  chatQuestionSources?: string[];
  freshnessMs?: number;
}
interface Entry {
  settings: SessionOptions;
  store: CodexStatusStore;
  questions: CodexChatQuestions;
}
const identity = (v: unknown): v is string => typeof v === 'string' && /^[\w-]{1,128}$/.test(v);
const normalized = (v: string) =>
  process.platform === 'win32' || /^[a-z]:[/\\]/i.test(v)
    ? path.win32.resolve(v).toLowerCase()
    : path.resolve(v);
const truncate = (s: string, bytes: number) => {
  let out = '';
  for (const c of s) {
    if (Buffer.byteLength(out + c) > bytes) break;
    out += c;
  }
  return out;
};
const jsonSize = (s: string) => Buffer.byteLength(JSON.stringify(s)) - 2;
const displayText = (s: string, budget: number) => {
  if (jsonSize(s) <= budget) return s;
  let text = truncate(s, Math.max(0, budget - 3));
  while (jsonSize(text + '…') > budget) text = [...text].slice(0, -1).join('');
  return text + '…';
};

/** One leased roster and one question owner per session, with no cross-session fallback. */
export class CodexSessions {
  private entries = new Map<string, Entry>();
  private owner?: string;
  private leaseUntil = 0;
  constructor(
    private readonly options: SessionOptions,
    private readonly now = Date.now
  ) {
    if (!['project', 'local', 'manual'].includes(options.sessionScope ?? ''))
      throw new Error('Invalid multi-session scope');
    if (options.sessionScope === 'project' && !options.codexCwd)
      throw new Error('Project scope requires a project directory');
    if (
      options.sessionScope === 'manual' &&
      (!options.sessionIds?.length ||
        options.sessionIds.length > 16 ||
        !options.sessionIds.every(identity))
    )
      throw new Error('Manual scope requires 1–16 session ids');
  }
  private refresh() {
    if (this.now() >= this.leaseUntil) {
      this.entries.clear();
      this.owner = undefined;
    }
  }
  sync(payload: unknown) {
    this.refresh();
    const p = payload as Record<string, unknown> | null;
    if (
      !p ||
      p.kind !== 'codex-sessions' ||
      typeof p.owner !== 'string' ||
      !/^[\w-]{16,128}$/.test(p.owner) ||
      !Array.isArray(p.sessions) ||
      p.sessions.length > 16 ||
      (p.release !== undefined && typeof p.release !== 'boolean') ||
      (p.release && p.sessions.length)
    )
      throw new InteractionError(400, 'Invalid session snapshot');
    if (this.owner && this.owner !== p.owner)
      throw new InteractionError(409, 'Another session collector is connected');
    const ids = new Set<string>();
    const rows: { id: string; name: string; status: { type: string; activeFlags: string[] } }[] =
      [];
    for (const input of p.sessions) {
      const s = input as Record<string, unknown>;
      const status = s?.status as { type?: unknown; activeFlags?: unknown } | undefined;
      if (
        !s ||
        !identity(s.id) ||
        ids.has(s.id) ||
        s.kind !== 'codex' ||
        s.hostId !== 'local' ||
        typeof s.name !== 'string' ||
        !s.name.trim() ||
        Buffer.byteLength(s.name) > 512 ||
        /\p{Cc}/u.test(s.name) ||
        typeof s.cwd !== 'string' ||
        !s.cwd ||
        !status ||
        !['active', 'idle', 'notLoaded', 'systemError'].includes(String(status.type)) ||
        (status.activeFlags !== undefined &&
          (!Array.isArray(status.activeFlags) ||
            status.activeFlags.length > 8 ||
            status.activeFlags.some((f) => typeof f !== 'string'))) ||
        (this.options.sessionScope === 'project' &&
          normalized(s.cwd) !== normalized(this.options.codexCwd!)) ||
        (this.options.sessionScope === 'manual' && !this.options.sessionIds!.includes(s.id))
      )
        throw new InteractionError(400, 'Out-of-scope or invalid Codex session');
      ids.add(s.id);
      rows.push({
        id: s.id,
        name: s.name,
        status: { type: String(status.type), activeFlags: (status.activeFlags as string[]) ?? [] }
      });
    }
    const next = new Map<string, Entry>();
    for (const row of rows) {
      const settings = { ...this.options, sessionId: row.id, label: row.name };
      const entry = this.entries.get(row.id) ?? {
        settings,
        store: new CodexStatusStore(settings, this.now),
        questions: new CodexChatQuestions(settings, this.now)
      };
      entry.settings.label = row.name;
      entry.store.ingest({
        hook_event_name: 'StatusSnapshot',
        session_id: row.id,
        status: row.status,
        emitted_at: this.now()
      });
      next.set(row.id, entry);
    }
    this.entries = next;
    this.owner = p.release ? undefined : p.owner;
    this.leaseUntil = p.release ? 0 : this.now() + 15000;
    return { ok: true, sessions: next.size };
  }
  ingest(event: CodexEvent) {
    this.refresh();
    return this.entries.get(event.session_id)?.store.ingest(event) ?? false;
  }
  syncQuestions(payload: unknown) {
    this.refresh();
    const id = (payload as { sessionId?: unknown } | null)?.sessionId;
    const entry = typeof id === 'string' ? this.entries.get(id) : undefined;
    if (!entry) throw new InteractionError(404, 'The question session is no longer monitored');
    return entry.questions.sync(payload);
  }
  answer(sessionId: unknown, uiId: unknown, actionId: unknown, source: unknown) {
    this.refresh();
    return (
      typeof sessionId === 'string' &&
      (this.entries.get(sessionId)?.questions.answer(uiId, actionId, source) ?? false)
    );
  }
  state(): SessionsMessage {
    this.refresh();
    const sessions: SessionsMessage['sessions'] = [...this.entries.values()].map((e) => {
      const s = e.store.state();
      return { session: s.session!, ui: e.questions.ui() ?? s.ui };
    });
    if (!sessions.length)
      sessions.push({
        session: {
          id: this.options.sessionId ?? 'codex-monitor',
          name: this.options.label ?? 'Codex'
        },
        ui: {
          id: 'session-collector-unavailable',
          source: 'codex',
          state: 'idle',
          message: 'セッション一覧の接続待ち・PCを確認',
          actions: [],
          updatedAt: 0
        }
      });
    // A fixed budget per row fits 16 cards in one 64 KiB frame. Never resize the
    // selected card when another session receives/finishes a long question:
    // CompactUi rightly treats changed text as a new reading/focus state.
    for (const s of sessions) {
      s.session = { ...s.session, name: displayText(s.session.name, 512) };
      if (s.ui?.actions)
        s.ui = {
          ...s.ui,
          actions: s.ui.actions.map((a) => ({ ...a, label: displayText(a.label, 128) }))
        };
      if (!s.ui?.message) continue;
      const empty = { ...s, ui: { ...s.ui, message: '' } };
      const budget = Math.max(0, 3700 - Buffer.byteLength(JSON.stringify(empty)));
      if (jsonSize(s.ui.message) > budget) {
        const suffix = '\n…全文はPCで確認';
        let message = truncate(s.ui.message, Math.max(0, budget - jsonSize(suffix)));
        while (jsonSize(message + suffix) > budget) message = [...message].slice(0, -1).join('');
        s.ui = { ...s.ui, message: message + suffix };
      }
    }
    return { type: 'sessions', sessions };
  }
}
