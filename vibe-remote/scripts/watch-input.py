#!/usr/bin/env python3
"""Ask a YES/NO question through the local Vibe bridge and return a structured answer."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import time
import urllib.request


class WatchInput:
    def __init__(self, config_path: Path):
        config = json.loads(config_path.read_text(encoding="utf-8-sig"))
        self.port = int(config["port"])
        self.token = config["token"]
        if not 1 <= self.port <= 65535 or not isinstance(self.token, str) or len(self.token) < 16:
            raise ValueError("Invalid local bridge configuration")
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def request(self, route: str, payload: dict):
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}{route}",
            json.dumps(payload, ensure_ascii=False).encode("utf-8"),
            {"Authorization": "Bearer " + self.token, "Content-Type": "application/json"},
            method="POST")
        with self.opener.open(request, timeout=2) as response:
            return json.load(response)

    def ask(self, *, kind: str, session_id: str, message: str,
            turn_id: str | None = None, timeout: int = 60):
        payload = {"kind": kind, "sessionId": session_id, "message": message,
                   "timeoutMs": timeout * 1000}
        if turn_id:
            payload["turnId"] = turn_id
        created = self.request("/interactions", payload)["interaction"]
        route = "/interactions/" + created["id"]
        receipt = {"receipt": created["receipt"]}
        finished = False
        try:
            deadline = time.monotonic() + timeout + 2
            while time.monotonic() < deadline:
                result = self.request(route + "/poll", receipt)["interaction"]
                if result["status"] != "pending":
                    finished = True
                    return result
                time.sleep(0.25)
            return {"id": created["id"], "status": "expired"}
        finally:
            # A stopped caller must not leave a live approval on the watch.
            if not finished:
                try:
                    self.request(route + "/cancel", receipt)
                except (OSError, ValueError):
                    pass  # The server also expires a missing caller's 5-second lease.


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--question", required=True, help="Exact YES/NO question shown on the watch")
    parser.add_argument("--session-id", default=os.environ.get("CODEX_THREAD_ID"))
    parser.add_argument("--turn-id", default=os.environ.get("CODEX_TURN_ID"))
    parser.add_argument("--timeout", type=int, default=60, choices=range(1, 121), metavar="1..120")
    args = parser.parse_args(argv)
    if not args.session_id:
        parser.error("--session-id (or CODEX_THREAD_ID) is required")
    try:
        result = WatchInput(args.config).ask(
            kind="question", session_id=args.session_id, turn_id=args.turn_id,
            message=args.question, timeout=args.timeout)
        print(json.dumps(result, ensure_ascii=True), flush=True)
        return 0 if result.get("status") == "answered" else 2
    except (OSError, ValueError, KeyError, TypeError):
        # Never report an unanswered question as NO or YES.
        print(json.dumps({"status": "unavailable", "answer": None}), flush=True)
        return 2
    except KeyboardInterrupt:
        print(json.dumps({"status": "cancelled", "answer": None}), flush=True)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
