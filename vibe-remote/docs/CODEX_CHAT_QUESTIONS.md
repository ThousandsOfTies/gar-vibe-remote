# Ordinary chat choice cards

`startCodexBridge` optionally accepts `chatQuestions: true`, one `sessionId`, and
an explicit `chatQuestionSources` device-source allowlist. This does not enable
`watchInput` or execution approvals. A client-specific adapter owns capture and
delivery to its original chat; the bridge does not read transcripts or control
another application's UI.

An authenticated `POST /chat-questions` supplies an authoritative snapshot:

```json
{"kind":"chat-choice","sessionId":"selected-chat","owner":"stable-private-adapter-id","questions":[{"id":"original-question-id","question":"Question and full choices","options":[{"id":"option-0","label":"First"},{"id":"option-1","label":"Second"}]}],"delivered":[],"failed":[]}
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
