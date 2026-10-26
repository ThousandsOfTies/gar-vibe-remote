# Ordinary chat choice cards

`startCodexBridge` optionally accepts `chatQuestions: true`, one `sessionId`, and
an explicit `chatQuestionSources` device-source allowlist. This does not enable
`watchInput` or execution approvals. A client-specific adapter owns capture and
delivery to its original chat; the bridge does not read transcripts or control
another application's UI.

An authenticated `POST /chat-questions` supplies an authoritative snapshot:

```json
{
  "kind": "chat-choice",
  "sessionId": "selected-chat",
  "owner": "stable-private-adapter-id",
  "questions": [
    {
      "id": "original-question-id",
      "question": "Question and full choices",
      "options": [
        { "id": "option-0", "label": "First" },
        { "id": "option-1", "label": "Second" }
      ]
    }
  ],
  "delivered": [],
  "failed": []
}
```

Poll at intervals shorter than five seconds. The response's `answers` contains
the original question ID, selected choice ID and device source. The adapter
rechecks the original card before delivering a physical selection; the next
snapshot reports delivered/failed IDs or removes cards answered on the PC.
Selection acknowledgement means the bridge received the choice, not that the
original application accepted it. A selected card has no remaining actions.

The five-second owner lease prevents abandoned adapters from leaving active
choices. Changing content under an existing ID, competing owners and stale UI
actions are rejected. At most 24 questions with 2–8 choices each are accepted.
Send an empty snapshot with `release: true` on shutdown. An adapter must never
replay an uncertain delivery or use a synthetic test as a human reply.

## Multiple local sessions

Set `sessionScope` to `project`, `local` or `manual`; retain an anchor `sessionId`.
Project mode requires `codexCwd`; manual mode requires 1–16 `sessionIds`.
The authenticated `POST /sessions` route takes an authoritative roster with a
15-second owner lease:

```json
{
  "kind": "codex-sessions",
  "owner": "stable-private-collector-id",
  "sessions": [
    {
      "id": "chat-a",
      "name": "Original chat title",
      "kind": "codex",
      "hostId": "local",
      "cwd": "D:\\Example",
      "status": { "type": "active", "activeFlags": [] }
    }
  ]
}
```

The bridge enforces local Codex, scope, duplicate IDs and the 16-session bound.
It publishes `type: sessions` snapshots. Each enrolled session owns an independent
status store and ordinary-question manager. `/chat-questions` retains the same
contract but uses the enrolled `sessionId`. A WebSocket `uiAction` **must** include
both `sessionId` and the current random `uiId`; it cannot fall back to another
session. Removal or expiry revokes every question belonging to that entry.
Execution interactions and input tests stay outside the live multi-session path.

The output uses a fixed display budget per session to stay under the physical
64 KiB limit without changing another card's text/focus as background questions
arrive or finish. Original reply labels remain in the caller's native-card model.
Display-only names/labels/body may be shortened. A body with over 200 lines is
rejected because it could exceed the LVGL coordinate budget after wrapping.
