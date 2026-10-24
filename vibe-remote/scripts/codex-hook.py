#!/usr/bin/env python3
"""Forward lifecycle metadata; optionally relay a PermissionRequest to the watch."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request

EVENTS = {"SessionStart", "SessionEnd", "UserPromptSubmit", "PreToolUse", "PermissionRequest",
          "PostToolUse", "Stop", "Interrupt", "StatusSnapshot"}


def watch_decision(event, config):
    """Return a one-operation decision only after the live request receives a tap."""
    inputs = event.get("tool_input")
    if not isinstance(inputs, dict) or not event.get("turn_id"):
        return {}
    # Forward the exact approval input, never infer a permission from lifecycle state.
    details = json.dumps(inputs, ensure_ascii=False, separators=(",", ":"))
    message = " | ".join(str(value) for value in
                         (event.get("tool_name", "tool"), event.get("cwd", ""), details))
    if len(message) > 1000:
        return {}  # Large patches/inputs remain on the PC; never truncate an approval.
    spec = importlib.util.spec_from_file_location("gar_watch_input", Path(__file__).with_name("watch-input.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.WatchInput(config).ask(
        kind="approval", session_id=event["session_id"], turn_id=event["turn_id"],
        message=message, timeout=120)
    if result.get("status") != "answered" or result.get("answer") not in ("yes", "no"):
        return {}
    decision = {"behavior": "allow" if result["answer"] == "yes" else "deny"}
    if result["answer"] == "no":
        decision["message"] = "The user declined this operation on Vibe Remote."
    return {"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": decision}}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--strict", action="store_true", help="Fail on delivery errors in a connection test")
    parser.add_argument("--watch-input", action="store_true", help="Wait for YES/NO on PermissionRequest only")
    args = parser.parse_args()
    try:
        raw = sys.stdin.buffer.read(1024 * 1024 + 1)
        if len(raw) > 1024 * 1024:
            raise ValueError("Hook input too large")
        event = json.loads(raw.decode("utf-8-sig"))
        if not isinstance(event, dict) or event.get("hook_event_name") not in EVENTS:
            raise ValueError("Unsupported hook event")
        # Never forward prompt, tool_input, tool_response, transcript or assistant text.
        metadata = {key: event[key] for key in
                    ("hook_event_name", "session_id", "turn_id", "cwd", "tool_name", "source")
                    if isinstance(event.get(key), str)}
        if event.get("hook_event_name") == "StatusSnapshot":
            metadata["status"] = event.get("status")
        metadata["emitted_at"] = int(time.time() * 1000)
        metadata["delivery"] = "test" if args.strict else "hook"
        config = json.loads(args.config.read_text(encoding="utf-8-sig"))
        port = int(config["port"])
        if not 1 <= port <= 65535:
            raise ValueError("Invalid bridge port")
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/events", json.dumps(metadata).encode("utf-8"),
            {"Content-Type": "application/json", "Authorization": "Bearer " + config["token"]},
            method="POST")
        # Ignore proxy settings for this local-only connection.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open(request, timeout=1.0) as response:
            result = json.load(response)
            if not result.get("ok"):
                raise ValueError("Event was not accepted")
        if (args.watch_input and not args.strict and result.get("accepted")
                and event["hook_event_name"] == "PermissionRequest"):
            decision = watch_decision(event, args.config)
            print(json.dumps(decision))
            return 0
    except (OSError, ValueError, KeyError, TypeError, urllib.error.URLError):
        # No answer, wrong session or a lost bridge returns control to the normal PC prompt.
        print("{}")
        if args.strict:
            print("Codex status delivery failed", file=sys.stderr)
            return 1
        return 0
    print("{}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
