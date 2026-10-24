#!/usr/bin/env python3
"""Read hook discovery and trust status through the official app-server API."""
from __future__ import annotations

import json
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time


def summarize_hooks(result: dict, sender: Path, config: Path) -> dict:
    def normalized(value):
        return str(value).replace("\\", "/").casefold()
    hooks = [hook for item in result.get("data", []) for hook in item.get("hooks", [])
             if normalized(sender) in normalized(hook.get("command", ""))
             and normalized(config) in normalized(hook.get("command", ""))]
    enabled = [hook for hook in hooks if hook.get("enabled")]
    trusted = [hook for hook in enabled if hook.get("isManaged") or hook.get("trustStatus") == "trusted"]
    review = [hook for hook in enabled if hook not in trusted]
    required = {"preToolUse", "permissionRequest", "postToolUse", "sessionStart",
                "sessionEnd", "userPromptSubmit", "stop", "interrupt"}
    covered = {hook.get("eventName") for hook in enabled}
    errors = any(item.get("errors") for item in result.get("data", []))
    state = "error" if errors else "missing" if not required.issubset(covered) else "review" if review else "ready"
    return {"state": state, "configured": len(hooks), "enabled": len(enabled),
            "trusted": len(trusted), "needsReview": len(review), "checkedAt": int(time.time() * 1000)}


def read_hook_setup(cwd: Path, config: Path, *, command=None, timeout=8.0) -> dict:
    codex = shutil.which("codex")
    if command is None and not codex:
        return {"state": "error", "message": "Codex CLIが見つかりません"}
    try:
        proc = subprocess.Popen([*(command or [codex]), "app-server", "--listen", "stdio://"],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, encoding="utf-8")
    except OSError:
        return {"state": "error", "message": "Codex CLIを起動できませんでした"}
    messages = queue.Queue(maxsize=100)

    def read():
        for line in proc.stdout:
            if len(line) > 1024 * 1024:
                continue
            try:
                messages.put_nowait(json.loads(line))
            except (ValueError, queue.Full):
                pass

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout

    def send(value):
        proc.stdin.write(json.dumps(value) + "\n")
        proc.stdin.flush()

    def rpc(identifier, method, params):
        send({"id": identifier, "method": method, "params": params})
        while time.monotonic() < deadline:
            response = messages.get(timeout=max(0.01, deadline - time.monotonic()))
            if response.get("id") != identifier:
                continue
            if "error" in response:
                raise ValueError("App-server API unavailable")
            return response["result"]
        raise TimeoutError()

    try:
        rpc(1, "initialize", {"clientInfo": {"name": "gar_codex_diagnostics", "version": "0.1.0"}})
        send({"method": "initialized", "params": {}})
        result = rpc(2, "hooks/list", {"cwds": [str(cwd.resolve())]})
        return summarize_hooks(result, Path(__file__).with_name("codex-hook.py"), config.resolve())
    except (OSError, ValueError, KeyError, TypeError, AttributeError, queue.Empty, TimeoutError):
        return {"state": "error", "message": "Codexの設定を確認できませんでした"}
    finally:
        proc.stdin.close()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        reader.join(timeout=1)
        proc.stdout.close()
