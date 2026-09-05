import json
import os
from pathlib import Path
import queue
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from bridge.sources import PcMetrics, parse_rate_limits, read_state, write_state, text
from bridge.service import Bridge, load_config


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name)

    def test_missing_is_offline(self):
        status, usage = read_state(self.path, "codex")
        self.assertFalse(status["online"]); self.assertIsNone(usage)

    def test_state_expiry_and_future(self):
        for stamp, expected in ((100, True), (80, False), (120, False)):
            write_state(self.path, "codex", {"online": True, "working": True, "ts": stamp})
            self.assertEqual(read_state(self.path, "codex", 100)[0]["online"], expected)

    def test_atomic_input_and_unknown_usage(self):
        write_state(self.path, "claude", {"online": True, "working": True, "task": "test", "ts": 100})
        status, usage = read_state(self.path, "claude", 101)
        self.assertTrue(status["working"]); self.assertIsNone(usage)
        self.assertEqual(list(self.path.glob("*.tmp")), [])
        (self.path / "claude.json").write_text("{")
        self.assertFalse(read_state(self.path, "claude", 101)[0]["online"])

    def test_bad_input_cannot_become_online(self):
        for data in ({"online": "true", "working": True, "ts": 100},
                     {"online": True, "working": True, "ts": "100"},
                     {"online": True, "working": True, "ts": 100, "pid": 999999999, "process_started": 1}):
            write_state(self.path, "codex", data)
            self.assertFalse(read_state(self.path, "codex", 100)[0]["online"])

    def test_usage_countdown(self):
        usage = {"five_hour": 21, "weekly": 40, "five_hour_reset": 20, "weekly_reset": 200}
        write_state(self.path, "codex", {"online": True, "working": False, "ts": 100, "usage": usage})
        self.assertEqual(read_state(self.path, "codex", 105)[1]["five_hour_reset"], 15)
        usage["five_hour"] = 101
        write_state(self.path, "codex", {"online": True, "working": False, "ts": 100, "usage": usage})
        self.assertIsNone(read_state(self.path, "codex", 105)[1])

    def test_official_quota_windows(self):
        limits = {"primary": {"windowDurationMins": 300, "usedPercent": 22, "resetsAt": 1100},
                  "secondary": {"windowDurationMins": 10080, "usedPercent": 45, "resetsAt": 2100}}
        self.assertEqual(parse_rate_limits({"rateLimits": limits}, 1000)["five_hour_reset"], 100)
        limits["primary"]["windowDurationMins"] = 15
        self.assertIsNone(parse_rate_limits({"rateLimits": limits}, 1000))

    def test_real_pc_metrics(self):
        sample = PcMetrics().sample()
        self.assertTrue(sample["online"])
        self.assertTrue(0 <= sample["cpu"] <= 100)
        self.assertTrue(0 <= sample["ram"] <= 100)
        self.assertGreaterEqual(sample["down_kbps"], 0)

    def test_utf8_bounds(self):
        value = text("真实任务" * 50, 79)
        self.assertLessEqual(len(value.encode()), 79)

    def test_config_validation(self):
        file = self.path / "config.json"
        file.write_text(json.dumps({"host": "localhost", "devices": ["bad/+"]}))
        with self.assertRaises(ValueError):
            load_config(file)


class CommandTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name)
        self.marker = self.path / "calls.txt"
        self.script = self.path / "handler.py"
        self.script.write_text("import sys,json\nfrom pathlib import Path\nd=json.load(sys.stdin)\np=Path(sys.argv[1])\nwith p.open('a') as f: f.write(d['action']+'\\n')\n")
        self.cfg = {"host": "127.0.0.1", "devices": ["deck"], "state_dir": str(self.path),
                    "handlers": {"codex": {"confirm": [sys.executable, str(self.script), str(self.marker)]}}}
        self.bridge = Bridge(self.cfg)
        write_state(self.path, "codex", {"online": True, "working": True, "ts": int(time.time())})
        self.worker = threading.Thread(target=self.bridge._worker)
        self.worker.start()

    def tearDown(self):
        self.bridge.stop.set(); self.worker.join(timeout=6)
        self.directory.cleanup()

    def envelope(self, **changes):
        now = int(time.time())
        return dict({"device_id": "deck", "id": "test-1", "action": "confirm", "ts": now, "expires_at": now + 10}, **changes)

    def send(self, data):
        self.bridge.commands.put(("codex", data))
        return self.bridge.outbox.get(timeout=3)[1]

    def test_command_and_duplicate(self):
        data = self.envelope()
        self.assertEqual(self.send(data)["status"], "completed")
        self.assertEqual(self.send(data)["status"], "completed")
        self.assertEqual(self.marker.read_text().splitlines(), ["confirm"])

    def test_duplicate_after_restart(self):
        data = self.envelope()
        self.send(data)
        self.bridge.stop.set(); self.worker.join(timeout=3)
        self.bridge = Bridge(self.cfg)
        self.worker = threading.Thread(target=self.bridge._worker); self.worker.start()
        self.assertEqual(self.send(data)["status"], "completed")
        self.assertEqual(self.marker.read_text().splitlines(), ["confirm"])

    def test_expired_unsupported_and_malformed(self):
        for data in (self.envelope(expires_at=1), self.envelope(action="stop"), self.envelope(action={})):
            self.assertEqual(self.send(data)["status"], "rejected")
        self.assertFalse(self.marker.exists())

    def test_dead_agent_rejects_command(self):
        write_state(self.path, "codex", {"online": False, "working": False, "ts": int(time.time())})
        self.assertEqual(self.send(self.envelope())["reason"], "agent_offline")

    def test_retained_and_unauthorized_never_queue(self):
        from types import SimpleNamespace
        for retained, device in ((True, "deck"), (False, "outsider")):
            msg = SimpleNamespace(retain=retained, topic="agent/codex/command",
                                  payload=json.dumps(self.envelope(device_id=device)).encode())
            self.bridge._message(None, None, msg)
        self.assertTrue(self.bridge.commands.empty())
        self.assertFalse(self.marker.exists())


if __name__ == "__main__":
    unittest.main()
