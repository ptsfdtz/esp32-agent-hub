"""Read only explicitly supplied state and real OS / Codex measurements."""
import json
import math
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time
import uuid

import psutil

AGENTS = ("codex", "claude", "opencode")


def text(value, limit):
    if not isinstance(value, str):
        return ""
    return value.replace("\x00", "").encode("utf-8")[:limit].decode("utf-8", errors="ignore")


def percentage(value):
    return type(value) in (int, float) and math.isfinite(value) and 0 <= value <= 100


def write_state(directory, agent, state):
    if agent not in AGENTS:
        raise ValueError("Unknown agent")
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    tmp = directory / f".{agent}-{uuid.uuid4().hex}.tmp"
    try:
        tmp.write_text(json.dumps(state, allow_nan=False), encoding="utf-8")
        os.replace(tmp, directory / f"{agent}.json")
    finally:
        tmp.unlink(missing_ok=True)


def read_state(directory, agent, now=None):
    now = time.time() if now is None else now
    offline = {"online": False, "working": False, "model": "", "task": "", "ts": int(now)}
    try:
        path = Path(directory) / f"{agent}.json"
        if path.stat().st_size > 4096:
            return offline, None
        state = json.loads(path.read_text(encoding="utf-8"))
        stamp = state["ts"]
        if type(stamp) not in (int, float) or not math.isfinite(stamp) or not -5 <= now - stamp < 15:
            return offline, None
        if type(state["online"]) is not bool or type(state["working"]) is not bool:
            return offline, None
        if "pid" in state:
            process = psutil.Process(state["pid"])
            if abs(process.create_time() - state["process_started"]) > .01:
                return offline, None
        status = dict(offline, online=state["online"], working=state["online"] and state["working"],
                      model=text(state.get("model"), 31), task=text(state.get("task"), 79))
        completed = state.get("completed_at", 0)
        if type(completed) is int and 0 <= completed <= int(now) + 5:
            status["completed_at"] = completed
        usage = state.get("usage")
        if not isinstance(usage, dict) or not all(percentage(usage.get(k)) for k in ("five_hour", "weekly")):
            usage = None
        elif not all(type(usage.get(k)) is int and 0 <= usage[k] <= 0xFFFFFFFF
                     for k in ("five_hour_reset", "weekly_reset")):
            usage = None
        else:
            usage = {k: usage[k] for k in ("five_hour", "weekly", "five_hour_reset", "weekly_reset")}
            for k in ("five_hour_reset", "weekly_reset"):
                usage[k] = max(0, usage[k] - int(max(0, now - stamp)))
        return status, usage
    except (OSError, ValueError, KeyError, TypeError, psutil.Error):
        return offline, None


class PcMetrics:
    def __init__(self):
        psutil.cpu_percent(None)
        self.previous = psutil.net_io_counters().bytes_recv
        self.at = time.monotonic()
        self.gpu = None
        self.gpu_at = 0

    def sample(self):
        now = time.monotonic()
        received = psutil.net_io_counters().bytes_recv
        down = max(0, received - self.previous) * 8 / max(.001, now - self.at) / 1000
        self.previous, self.at = received, now
        if now - self.gpu_at >= 10:
            self.gpu_at = now
            self.gpu = None
            executable = shutil.which("nvidia-smi")
            if executable:
                try:
                    result = subprocess.run([executable, "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"],
                                            capture_output=True, text=True, timeout=2, check=True,
                                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    values = [float(v) for v in result.stdout.splitlines()]
                    if values and all(percentage(v) for v in values):
                        self.gpu = max(values)
                except (OSError, ValueError, subprocess.SubprocessError):
                    pass
        return {"online": True, "cpu": psutil.cpu_percent(None), "ram": psutil.virtual_memory().percent,
                "gpu": self.gpu, "down_kbps": min(0xFFFFFFFF, round(down)), "ts": int(time.time())}


def parse_rate_limits(result, now):
    buckets = result.get("rateLimitsByLimitId") or {}
    limits = buckets.get("codex") or result.get("rateLimits") or {}
    windows = {w.get("windowDurationMins"): w for w in (limits.get("primary"), limits.get("secondary")) if isinstance(w, dict)}
    # Never label a different plan window (e.g. 15 min) as a five-hour quota.
    short, week = windows.get(300), windows.get(10080)
    if not short or not week:
        return None
    if not all(percentage(w.get("usedPercent")) and type(w.get("resetsAt")) is int for w in (short, week)):
        return None
    return {"five_hour": short["usedPercent"], "weekly": week["usedPercent"],
            "five_hour_reset": max(0, short["resetsAt"] - now), "weekly_reset": max(0, week["resetsAt"] - now)}


class CodexUsage:
    """Read-only app-server RPC. No prompts, login flow, or credential-file parsing."""
    def __init__(self, command):
        self.command = command
        self.lock = threading.Lock()
        self.latest = None
        self.measured = 0
        self.stop = threading.Event()
        self.thread = None

    def start(self):
        if self.command:
            self.thread = threading.Thread(target=self._run, daemon=True)
            self.thread.start()

    def sample(self):
        with self.lock:
            elapsed = int(time.time() - self.measured)
            if self.latest is None or elapsed > 120:
                return None
            result = dict(self.latest)
            for k in ("five_hour_reset", "weekly_reset"):
                result[k] = max(0, result[k] - elapsed)
            return result

    def _query(self):
        argv = list(self.command)
        argv[0] = shutil.which(argv[0]) or argv[0]
        process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                   text=True, encoding="utf-8", creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        messages = queue.Queue(maxsize=32)

        def read():
            try:
                for line in process.stdout:
                    if len(line) > 65536:
                        continue
                    try:
                        item = json.loads(line)
                        if isinstance(item, dict) and item.get("id") in (1, 2):
                            messages.put_nowait(item)
                    except (ValueError, queue.Full):
                        pass
            except (OSError, ValueError):
                pass

        reader = threading.Thread(target=read, daemon=True)
        reader.start()

        def send(data):
            process.stdin.write(json.dumps(data) + "\n")
            process.stdin.flush()

        def response(expected):
            deadline = time.monotonic() + 12
            while not self.stop.is_set() and time.monotonic() < deadline:
                try:
                    item = messages.get(timeout=.2)
                    if item.get("id") == expected:
                        if "error" in item:
                            raise ValueError("Codex app-server rejected read-only request")
                        return item["result"]
                except queue.Empty:
                    if process.poll() is not None:
                        break
            raise TimeoutError("Codex app-server did not respond")

        try:
            send({"id": 1, "method": "initialize", "params": {"clientInfo": {"name": "agentdeck_bridge", "version": "0.2.0"}}})
            response(1)
            send({"method": "initialized", "params": {}})
            send({"id": 2, "method": "account/rateLimits/read"})
            return parse_rate_limits(response(2), int(time.time()))
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=3)
            reader.join(timeout=1)
            process.stdin.close(); process.stdout.close()

    def _run(self):
        while not self.stop.is_set():
            try:
                usage = self._query()
            except (OSError, ValueError, KeyError, TypeError, TimeoutError, subprocess.SubprocessError):
                usage = None
            with self.lock:
                self.latest = usage; self.measured = time.time()
            self.stop.wait(60)
