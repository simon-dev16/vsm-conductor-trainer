"""Loopback-only test fixture. Synthetic payloads; not a backend or an AI implementation."""
import argparse
import datetime as dt
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def timestamp(seconds=0):
    return (dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=seconds)).isoformat().replace("+00:00", "Z")


def run(completed=False):
    return {
        "runId": "fixture-run", "revision": int(completed),
        "status": "completed" if completed else "active", "serverTime": timestamp(),
        "state": {"safety": 100, "loyalty": {"passenger_01": 50}, "competencies": {}},
        "currentNode": {"id": "fixture-turn-2" if completed else "fixture-turn-1",
                        "actorId": "passenger_01", "text": "Synthetic HTTP fixture",
                        "deadlineAt": None if completed else timestamp(120), "choices": []},
        "commands": [], "assessments": [], "debrief": "HTTP roundtrip verified" if completed else ""
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass  # Never log request bodies, credentials or authorization headers.

    def send(self, code, body):
        raw = body.encode() if isinstance(body, str) else json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        try:
            self.wfile.write(raw)
        except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError):
            pass

    def error(self, status, code):
        self.send(status, {"error": {"code": code, "message": "Synthetic test error"}})

    def do_GET(self):
        if self.path == "/health":
            return self.send(200, {"fixture": "vsm-http-tests-v1"})
        if self.headers.get("Authorization") != "Bearer fixture-token":
            return self.error(401, "unauthorized")
        if self.path == "/v1/scenarios":
            return self.send(200, {"items": [{"key": "luggage-help", "version": 1, "title": "Synthetic fixture", "type": "service"}]})
        item = self.path.rsplit("/", 1)[-1]
        if item.startswith("status") and item[6:].isdigit():
            return self.error(int(item[6:]), "fixture_" + item)
        if item == "malformed":
            return self.send(200, "{not-json")
        if item == "wrong-schema":
            return self.send(200, {"runId": item, "state": []})
        if item == "wrong-run":
            return self.send(200, run())
        if item == "stale":
            data = run()
            data["runId"] = "fixture-run"
            return self.send(200, data)
        return self.send(200, self.server.snapshot)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length > 20000:
            return self.error(413, "too_large")
        try:
            body = json.loads(self.rfile.read(length))
        except (ValueError, TypeError):
            return self.error(400, "invalid_json")
        if self.path == "/v1/auth/login":
            if body.get("login") == "slow":
                time.sleep(.8)
            if not body.get("password"):
                return self.error(422, "invalid_credentials")
            return self.send(200, {"accessToken": "fixture-token", "user": {"id": "fixture-user", "displayName": "Test conductor"}})
        if self.headers.get("Authorization") != "Bearer fixture-token":
            return self.error(401, "unauthorized")
        if self.path == "/v1/runs":
            if body != {"scenarioKey": "luggage-help", "scenarioVersion": 1, "mode": "ai_text"}:
                return self.error(422, "invalid_start_contract")
            self.server.snapshot = run()
            self.server.actions.clear()
            return self.send(201, self.server.snapshot)
        if self.path == "/v1/runs/fixture-run/timeout":
            return self.error(409, "deadline_not_elapsed")
        if self.path == "/v1/runs/fixture-run/ai-turn":
            key = body.get("clientActionId")
            if not key or not body.get("text") or "expectedRevision" not in body or not body.get("turnId"):
                return self.error(422, "invalid_turn_contract")
            with self.server.action_lock:
                if key in self.server.actions:
                    return self.send(200, self.server.actions[key])
                if body["expectedRevision"] != self.server.snapshot["revision"]:
                    return self.error(409, "stale_revision")
                self.server.snapshot = run(True)
                self.server.actions[key] = self.server.snapshot
                return self.send(200, self.server.snapshot)
        self.error(404, "not_found")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18765)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.snapshot = run()
    server.actions = {}
    server.action_lock = threading.Lock()
    print(f"VSM fixture listening on 127.0.0.1:{args.port}", flush=True)
    server.serve_forever()
