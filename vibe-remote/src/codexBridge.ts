import * as http from 'node:http';
import * as crypto from 'node:crypto';
import * as fs from 'node:fs';
import { WebSocket, WebSocketServer } from 'ws';
import { CodexStatusStore, parseCodexEvent } from './codexStatus';
import { CodexInteractions, InteractionError } from './codexInteraction';
import { CodexChatQuestions } from './codexChatQuestions';
import type { StateMessage } from './protocol';

export interface CodexBridgeOptions {
  token: string;
  port?: number;
  sessionId?: string;
  label?: string;
  freshnessMs?: number;
  now?: () => number;
  watchInput?: boolean;
  chatQuestions?: boolean;
  chatQuestionSources?: string[];
}

export async function startCodexBridge(options: CodexBridgeOptions) {
  if (!options.token || options.token.length < 16)
    throw new Error('A token of at least 16 characters is required');
  const store = new CodexStatusStore(options, options.now);
  const clients = new Set<WebSocket>();
  const now = options.now ?? Date.now;
  const interactions = new CodexInteractions(options, now);
  const chatQuestions = new CodexChatQuestions(options, now);
  let inputTest:
    | {
        id: string;
        createdAt: number;
        expiresAt: number;
        answer?: 'yes' | 'no';
        answeredAt?: number;
        source?: string;
      }
    | undefined;
  const testStatus = () =>
    !inputTest
      ? undefined
      : {
          ...inputTest,
          status: inputTest.answer
            ? 'answered'
            : now() >= inputTest.expiresAt
              ? 'expired'
              : 'pending'
        };
  const currentState = (): StateMessage => {
    const state = store.state();
    // The selector describes this session even when a question temporarily owns the screen.
    // Input routing still uses the original interaction id/session/turn checks.
    const input = chatQuestions.ui() ?? interactions.ui();
    if (input) return { ...state, ui: input };
    if (!inputTest) return state;
    const pending = !inputTest.answer && now() < inputTest.expiresAt;
    const finishedAt = inputTest.answeredAt ?? inputTest.expiresAt;
    if (!pending && now() >= finishedAt + 8000) return state;
    return {
      ...state,
      ui: {
        id: inputTest.id,
        title: '入力テスト',
        source: 'input-test',
        state: pending ? 'waiting' : inputTest.answer ? 'done' : 'idle',
        stateLabel: pending ? undefined : '',
        message: pending
          ? 'PCとの接続テスト'
          : inputTest.answer
            ? `${inputTest.answer.toUpperCase()}を送信しました`
            : 'テスト時間切れ',
        actions: pending
          ? [
              { id: 'yes', label: 'YES' },
              { id: 'no', label: 'NO' }
            ]
          : [],
        updatedAt: inputTest.answeredAt ?? inputTest.createdAt
      }
    };
  };
  const wss = new WebSocketServer({ noServer: true, maxPayload: 8192 });
  const authenticated = (token: unknown) => {
    if (typeof token !== 'string') return false;
    const actual = Buffer.from(token),
      expected = Buffer.from(options.token);
    return actual.length === expected.length && crypto.timingSafeEqual(actual, expected);
  };
  const sendState = (socket: WebSocket) => {
    if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(currentState()));
  };
  const broadcast = () => {
    for (const socket of clients) sendState(socket);
  };
  const json = (res: http.ServerResponse, status: number, payload: unknown) => {
    res.writeHead(status, {
      'Content-Type': 'application/json; charset=utf-8',
      'Cache-Control': 'no-store'
    });
    res.end(JSON.stringify(payload));
  };
  const server = http.createServer((req, res) => {
    const url = req.url?.split('?')[0];
    if (req.method === 'GET' && url === '/health') {
      json(res, 200, { ok: true, pid: process.pid, clients: clients.size });
      return;
    }
    if (!authenticated(req.headers.authorization?.replace(/^Bearer /, ''))) {
      json(res, 401, { error: 'Authentication required' });
      return;
    }
    if (req.method === 'GET' && url === '/state') {
      json(res, 200, { state: currentState(), ...store.summary() });
      return;
    }
    if (req.method === 'GET' && url === '/input-test') {
      json(res, 200, { test: testStatus() ?? null });
      return;
    }
    const interactionRoute = url?.match(/^\/interactions\/([\w-]+)\/(poll|cancel)$/);
    if (
      req.method !== 'POST' ||
      (url !== '/events' &&
        url !== '/input-test' &&
        url !== '/interactions' &&
        url !== '/chat-questions' &&
        !interactionRoute)
    ) {
      json(res, 404, { error: 'Not found' });
      return;
    }
    if (!req.headers['content-type']?.startsWith('application/json')) {
      json(res, 415, { error: 'Expected application/json' });
      return;
    }
    let size = 0;
    const maximum = url === '/chat-questions' ? 262144 : 16384;
    const chunks: Buffer[] = [];
    req.on('data', (chunk: Buffer) => {
      size += chunk.length;
      if (size <= maximum) chunks.push(chunk);
    });
    req.on('end', () => {
      if (size > maximum) {
        json(res, 413, { error: 'Event too large' });
        return;
      }
      try {
        const payload: unknown = JSON.parse(Buffer.concat(chunks).toString('utf8'));
        if (url === '/chat-questions') {
          if (testStatus()?.status === 'pending' || interactions.pending)
            throw new InteractionError(409, 'Another device question is pending');
          const result = chatQuestions.sync(payload);
          broadcast();
          json(res, 200, result);
          return;
        }
        if (url === '/interactions') {
          if (testStatus()?.status === 'pending' || chatQuestions.pending)
            throw new InteractionError(409, 'An input test is pending');
          const interaction = interactions.create(payload, store.summary());
          broadcast();
          json(res, 201, { interaction });
          return;
        }
        if (interactionRoute) {
          const receipt = (payload as { receipt?: unknown } | null)?.receipt;
          const interaction = interactions.poll(
            interactionRoute[1],
            receipt,
            interactionRoute[2] === 'cancel'
          );
          if (interactionRoute[2] === 'cancel') broadcast();
          json(res, 200, { interaction });
          return;
        }
        if (url === '/input-test') {
          if (
            !payload ||
            typeof payload !== 'object' ||
            Array.isArray(payload) ||
            Object.keys(payload).length
          ) {
            json(res, 400, { error: 'Input test accepts an empty object only' });
            return;
          }
          if (testStatus()?.status === 'pending' || interactions.pending || chatQuestions.pending) {
            json(res, 409, { error: 'An input test is already pending', test: testStatus() });
            return;
          }
          inputTest = {
            id: `input-test-${crypto.randomBytes(10).toString('hex')}`,
            createdAt: now(),
            expiresAt: now() + 120000
          };
          broadcast();
          json(res, 201, { test: testStatus() });
          return;
        }
        const event = parseCodexEvent(payload);
        const accepted = store.ingest(event);
        if (accepted) {
          interactions.observe(event);
          broadcast();
        }
        json(res, 200, { ok: true, accepted });
      } catch (error) {
        json(res, error instanceof InteractionError ? error.status : 400, {
          error: error instanceof InteractionError ? error.message : 'Invalid Codex lifecycle event'
        });
      }
    });
    req.on('error', () => {
      if (!res.writableEnded) res.destroy();
    });
  });
  server.on('upgrade', (req, socket, head) => {
    if (req.url !== '/' || (req.headers.origin && !loopbackOrigin(req.headers.origin))) {
      socket.end('HTTP/1.1 403 Forbidden\r\n\r\n');
      return;
    }
    wss.handleUpgrade(req, socket, head, (ws) => wss.emit('connection', ws, req));
  });
  wss.on('connection', (socket) => {
    const timer = setTimeout(() => socket.close(1008, 'Authentication timeout'), 5000);
    socket.on('error', () => {});
    socket.on('close', () => {
      clearTimeout(timer);
      clients.delete(socket);
    });
    socket.on('message', (raw) => {
      try {
        const message = JSON.parse(raw.toString()) as {
          type?: string;
          token?: string;
          uiId?: string;
          actionId?: string;
          source?: string;
        };
        if (!authenticated(message.token)) {
          socket.send(JSON.stringify({ type: 'ack', ok: false, error: '認証に失敗しました' }));
          socket.close(1008, 'Authentication failed');
          return;
        }
        if (
          message.type === 'uiAction' &&
          clients.has(socket) &&
          chatQuestions.answer(message.uiId, message.actionId, message.source)
        ) {
          socket.send(JSON.stringify({ type: 'ack', ok: true }));
          broadcast();
          return;
        }
        if (
          message.type === 'uiAction' &&
          clients.has(socket) &&
          interactions.answer(message.uiId, message.actionId, message.source)
        ) {
          socket.send(JSON.stringify({ type: 'ack', ok: true }));
          broadcast();
          return;
        }
        if (
          message.type === 'uiAction' &&
          clients.has(socket) &&
          inputTest &&
          message.uiId === inputTest.id &&
          testStatus()?.status === 'pending' &&
          (message.actionId === 'yes' || message.actionId === 'no')
        ) {
          inputTest.answer = message.actionId;
          inputTest.answeredAt = now();
          inputTest.source =
            typeof message.source === 'string'
              ? message.source.replace(/\p{Cc}/gu, ' ').slice(0, 80)
              : 'watch';
          socket.send(JSON.stringify({ type: 'ack', ok: true }));
          broadcast();
          return;
        }
        if (message.type !== 'hello' && message.type !== 'ping') {
          socket.send(
            JSON.stringify({
              type: 'ack',
              ok: false,
              error: '現在の質問・承認に対応しない操作です'
            })
          );
          return;
        }
        clearTimeout(timer);
        clients.add(socket);
        socket.send(JSON.stringify({ type: 'ack', ok: true }));
        sendState(socket);
      } catch {
        socket.close(1007, 'Invalid JSON');
      }
    });
  });
  server.requestTimeout = 5000;
  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(options.port ?? 39275, '127.0.0.1', () => {
      server.off('error', reject);
      resolve();
    });
  });
  const port = (server.address() as { port: number }).port;
  // Pings refresh the view, but never renew the freshness of a Codex observation.
  const ticker = setInterval(broadcast, 1000);
  ticker.unref();
  let closing: Promise<void> | undefined;
  return {
    store,
    port,
    url: `ws://127.0.0.1:${port}`,
    close: () =>
      (closing ??= (async () => {
        clearInterval(ticker);
        for (const socket of wss.clients) socket.terminate();
        await new Promise<void>((resolve) => wss.close(() => resolve()));
        await new Promise<void>((resolve, reject) =>
          server.close((error) => (error ? reject(error) : resolve()))
        );
      })())
  };
}

function loopbackOrigin(value: string) {
  try {
    const url = new URL(value);
    return (
      ['http:', 'https:'].includes(url.protocol) &&
      ['127.0.0.1', 'localhost', '[::1]'].includes(url.hostname)
    );
  } catch {
    return false;
  }
}

async function main() {
  const configIndex = process.argv.indexOf('--config');
  if (configIndex < 0 || !process.argv[configIndex + 1]) {
    throw new Error('Usage: node dist/codexBridge.js --config /path/to/private-config.json');
  }
  const config = JSON.parse(
    fs.readFileSync(process.argv[configIndex + 1], 'utf8').replace(/^\uFEFF/, '')
  );
  if (!Number.isInteger(config.port) || config.port < 1 || config.port > 65535)
    throw new Error('Invalid port');
  const bridge = await startCodexBridge(config);
  console.log(`Codex status bridge: ${bridge.url}`);
  console.log('Lifecycle input: POST /events. Watch replies: POST /interactions (authenticated).');
  for (const signal of ['SIGINT', 'SIGTERM'] as const)
    process.once(signal, () => {
      void bridge.close();
    });
}

if (require.main === module) {
  void main().catch((error: Error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
