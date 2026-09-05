import argparse
from contextlib import closing
import json
import logging
import os
from pathlib import Path
import queue
import re
import signal
import sqlite3
import subprocess
import threading
import time

import paho.mqtt.client as mqtt

from .sources import AGENTS, CodexUsage, PcMetrics, read_state

LOG = logging.getLogger("agentdeck")
IDENTIFIER = re.compile(r"[A-Za-z0-9_-]{1,32}\Z")
ACTIONS = {"confirm", "cancel", "stop", "pause", "resume", "approve", "reject", "retry"}


def load_config(path):
    path = Path(path).resolve()
    cfg = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(cfg.get("host"), str) or not cfg["host"]:
        raise ValueError("host is required")
    if type(cfg.get("port", 1883)) is not int or not 1 <= cfg.get("port", 1883) <= 65535:
        raise ValueError("Invalid MQTT port")
    if not cfg.get("devices") or not all(isinstance(d, str) and IDENTIFIER.fullmatch(d) for d in cfg["devices"]):
        raise ValueError("devices must list authorized device IDs")
    if not 1 <= cfg.get("interval", 3) <= 5:
        raise ValueError("interval must be 1..5 seconds")
    for agent, actions in cfg.get("handlers", {}).items():
        if agent not in AGENTS or not isinstance(actions, dict):
            raise ValueError("Invalid handlers")
        for action, argv in actions.items():
            if action not in ACTIONS or not isinstance(argv, list) or not argv or not all(isinstance(a, str) and a for a in argv):
                raise ValueError("Handlers must be fixed, nonempty argv lists")
    cfg["state_dir"] = str((path.parent / cfg.get("state_dir", ".state")).resolve())
    return cfg


class Bridge:
    def __init__(self, config):
        self.cfg = config
        self.stop = threading.Event()
        self.connected = threading.Event()
        self.commands = queue.Queue(maxsize=16)
        self.outbox = queue.Queue(maxsize=64)
        self.state_dir = Path(config["state_dir"])
        self.state_dir.mkdir(parents=True, exist_ok=True)
        self.metrics = PcMetrics()
        self.usage = CodexUsage(config.get("codex_usage_command"))
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                  client_id=config.get("client_id", "agentdeck-pc-main"), clean_session=True)
        user = os.environ.get(config.get("username_env", "AGENTDECK_MQTT_USER"))
        password = os.environ.get(config.get("password_env", "AGENTDECK_MQTT_PASSWORD"))
        if user:
            self.client.username_pw_set(user, password)
        if config.get("tls_ca"):
            self.client.tls_set(ca_certs=config["tls_ca"])
        self.client.reconnect_delay_set(1, 60)
        self.client.max_queued_messages_set(32)
        self.client.will_set("pc/status", json.dumps({"online": False, "ts": int(time.time())}), qos=1, retain=True)
        self.client.on_connect = self._connect
        self.client.on_disconnect = self._disconnect
        self.client.on_message = self._message

    def _connect(self, client, userdata, flags, reason, properties):
        if reason.is_failure:
            LOG.warning("MQTT connection rejected: %s", reason)
            return
        result, _ = client.subscribe([(f"agent/{agent}/command", 1) for agent in AGENTS])
        if result == mqtt.MQTT_ERR_SUCCESS:
            self.connected.set()
            LOG.info("MQTT connected")

    def _disconnect(self, client, userdata, flags, reason, properties):
        self.connected.clear()
        LOG.info("MQTT disconnected")

    def _message(self, client, userdata, message):
        if message.retain or len(message.payload) > 1024:
            return
        parts = message.topic.split("/")
        if len(parts) != 3 or parts[0] != "agent" or parts[1] not in AGENTS or parts[2] != "command":
            return
        try:
            data = json.loads(message.payload)
            if not isinstance(data, dict):
                return
            if data.get("device_id") not in self.cfg["devices"]:
                return
            if not isinstance(data.get("id"), str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", data["id"]):
                return
            self.commands.put_nowait((parts[1], data))
        except (ValueError, UnicodeError, queue.Full):
            LOG.warning("Invalid command or command queue full")

    def ack(self, data, status, reason=""):
        body = {"id": data["id"], "status": status, "reason": reason, "ts": int(time.time())}
        try:
            self.outbox.put_nowait((f"agentdeck/{data['device_id']}/ack", body))
        except queue.Full:
            LOG.warning("Acknowledgement queue full")

    def _worker(self):
        # Persist reservation before side effects: duplicate QoS1 deliveries and bridge
        # restarts cannot execute the same id twice. A crash is reported as unknown.
        with closing(sqlite3.connect(self.state_dir / "commands.sqlite3")) as db:
            db.execute("CREATE TABLE IF NOT EXISTS commands (device TEXT, id TEXT, agent TEXT, action TEXT, status TEXT, ts INTEGER, PRIMARY KEY(device,id))")
            while not self.stop.is_set():
                try:
                    agent, data = self.commands.get(timeout=.2)
                except queue.Empty:
                    continue
                now = int(time.time())
                action = data.get("action")
                if not isinstance(action, str) or action not in ACTIONS or type(data.get("ts")) is not int or type(data.get("expires_at")) is not int or not (
                        now - 15 <= data["ts"] <= now + 5 and now <= data["expires_at"] <= now + 30):
                    self.ack(data, "rejected", "expired_or_invalid"); continue
                key = (data["device_id"], data["id"])
                existing = db.execute("SELECT agent, action, status FROM commands WHERE device=? AND id=?", key).fetchone()
                if existing:
                    same = existing[:2] == (agent, action)
                    self.ack(data, existing[2] if same and existing[2] != "executing" else "rejected", "duplicate_or_unknown")
                    continue
                status, _ = read_state(self.state_dir, agent)
                argv = self.cfg.get("handlers", {}).get(agent, {}).get(action)
                if not status["online"] or not argv:
                    self.ack(data, "rejected", "agent_offline" if not status["online"] else "unsupported_action")
                    continue
                db.execute("INSERT INTO commands VALUES(?,?,?,?,?,?)", (*key, agent, action, "executing", now)); db.commit()
                result = "failed"
                try:
                    # The broker supplies no executable, arguments or shell text.
                    # The trusted handler receives the validated envelope via stdin.
                    completed = subprocess.run(argv, input=json.dumps(dict(data, agent=agent)), text=True,
                                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5,
                                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                    result = "completed" if completed.returncode == 0 else "failed"
                except (OSError, subprocess.SubprocessError):
                    pass
                db.execute("UPDATE commands SET status=? WHERE device=? AND id=?", (result, *key))
                db.execute("DELETE FROM commands WHERE ts < ?", (now - 86400,)); db.commit()
                self.ack(data, result)

    def publish(self, topic, body, retain=False):
        if self.connected.is_set():
            # Telemetry is QoS0 and never buffered across disconnects. Commands/acks
            # have explicit application ids and expiry instead of hidden retries.
            return self.client.publish(topic, json.dumps(body, ensure_ascii=False, allow_nan=False), qos=0, retain=retain)
        return None

    def sample(self):
        self.publish("pc/status", self.metrics.sample())
        for agent in AGENTS:
            status, usage = read_state(self.state_dir, agent)
            if agent == "codex":
                usage = self.usage.sample() or usage
            self.publish(f"agent/{agent}/status", status)
            self.publish(f"agent/{agent}/usage", dict(usage or {"available": False}, ts=int(time.time())))

    def run(self):
        worker = threading.Thread(target=self._worker, daemon=True)
        worker.start(); self.usage.start()
        self.client.connect_async(self.cfg["host"], self.cfg.get("port", 1883), keepalive=10)
        self.client.loop_start()
        next_sample = 0
        try:
            while not self.stop.wait(.05):
                if time.monotonic() >= next_sample:
                    next_sample = time.monotonic() + self.cfg.get("interval", 3)
                    if self.connected.is_set():
                        try:
                            self.sample()
                        except (OSError, ValueError) as error:
                            LOG.warning("Data source unavailable: %s", type(error).__name__)
                for _ in range(16):
                    try:
                        topic, body = self.outbox.get_nowait()
                    except queue.Empty:
                        break
                    if int(time.time()) - body["ts"] <= 15:
                        self.publish(topic, body)
        finally:
            self.stop.set(); self.usage.stop.set(); worker.join(timeout=6)
            for agent in AGENTS:
                self.publish(f"agent/{agent}/status", {"online": False, "working": False, "ts": int(time.time())})
            sent = self.publish("pc/status", {"online": False, "ts": int(time.time())}, retain=True)
            if sent:
                try:
                    sent.wait_for_publish(timeout=1)
                except RuntimeError:
                    pass
            self.client.disconnect(); self.client.loop_stop()
            if self.usage.thread:
                self.usage.thread.join(timeout=5)


def main():
    parser = argparse.ArgumentParser(description="Agent Deck real MQTT bridge")
    parser.add_argument("--config", default="bridge/config.json")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    bridge = Bridge(load_config(args.config))
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: bridge.stop.set())
    bridge.run()


if __name__ == "__main__":
    main()
