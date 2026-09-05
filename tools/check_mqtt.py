"""Real loopback MQTT integration test; requires amqtt==0.11.3 (test only)."""
import asyncio
import json
import logging
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
os.chdir(ROOT)
import paho.mqtt.client as mqtt
from amqtt.broker import Broker
from bridge.service import Bridge


async def broker_process(port):
    broker = Broker({"listeners": {"default": {"type": "tcp", "bind": f"127.0.0.1:{port}"}},
                     "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}}})
    await broker.start()
    await asyncio.Event().wait()


class LocalBroker:
    def __init__(self, port):
        self.port = port
        self.process = None

    def start(self):
        self.process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "--broker", str(self.port)],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        def listening():
            if self.process.poll() is not None:
                raise AssertionError("Test broker exited")
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=.2):
                    return True
            except OSError:
                return False
        wait_for(listening)

    def down(self):
        # Abrupt process loss deliberately tests real TCP disconnect/recovery.
        if self.process and self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)

    def close(self):
        self.down()


def wait_for(predicate, seconds=10):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        value = predicate()
        if value:
            return value
        time.sleep(.05)
    raise AssertionError("Timed out waiting for integration condition")


def main():
    logging.basicConfig(level=logging.ERROR)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0)); port = sock.getsockname()[1]
    broker = LocalBroker(port)
    broker.start()
    messages = queue.Queue()
    subscriber = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="integration-deck")
    subscriber.on_connect = lambda c, u, f, r, p: c.subscribe("#", 1)
    subscriber.on_message = lambda c, u, msg: messages.put((msg.topic, bytes(msg.payload)))
    subscriber.reconnect_delay_set(1, 2)
    subscriber.connect("127.0.0.1", port, 5); subscriber.loop_start()
    wait_for(subscriber.is_connected)
    parser = subprocess.Popen([str(ROOT / "build/host" / ("network.exe" if os.name == "nt" else "network")), "--stdin"],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")

    def message(topic, condition=lambda d: True, seconds=12):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                t, body = messages.get(timeout=.2)
            except queue.Empty:
                continue
            if t == topic:
                decoded = json.loads(body)
                if condition(decoded):
                    return decoded
        raise AssertionError("No expected message on " + topic)

    runner = None; bridge = None; thread = None
    temporary = tempfile.TemporaryDirectory()
    try:
        directory = Path(temporary.name)
        marker = directory / "calls.txt"
        handler = directory / "handler.py"
        handler.write_text("import sys,json\nfrom pathlib import Path\nd=json.load(sys.stdin)\nwith Path(sys.argv[1]).open('a') as f: f.write(d['id']+'\\n')\n")
        cfg = {"host": "127.0.0.1", "port": port, "devices": ["deck"], "interval": 1,
               "state_dir": str(directory), "handlers": {"codex": {
                   "confirm": [sys.executable, str(handler), str(marker)],
                   "stop": [sys.executable, "-m", "bridge.agent", "control", "--state-dir", str(directory)]}}}
        stamp = int(time.time())
        envelope = {"id": "retained", "device_id": "deck", "action": "confirm", "ts": stamp, "expires_at": stamp + 10}
        subscriber.publish("agent/codex/command", json.dumps(envelope), qos=1, retain=True).wait_for_publish(2)
        bridge = Bridge(cfg)
        thread = threading.Thread(target=bridge.run)
        thread.start(); wait_for(bridge.connected.is_set)
        pc = message("pc/status", lambda p: p.get("online"))
        parser.stdin.write("pc/status\n" + json.dumps(pc) + "\n"); parser.stdin.flush()
        assert parser.stdout.readline().strip() == "ok", "Production C++ parser rejected real PC telemetry"
        assert not marker.exists(), "Retained control executed"
        subscriber.publish("agent/codex/command", b"", retain=True).wait_for_publish(2)
        runner = subprocess.Popen([sys.executable, "-m", "bridge.agent", "run", "--agent", "codex", "--task", "integration process",
                                   "--state-dir", str(directory), "--", sys.executable, "-c", "import time; time.sleep(45)"])
        status = message("agent/codex/status", lambda p: p.get("working"))
        parser.stdin.write("agent/codex/status\n" + json.dumps(status) + "\n"); parser.stdin.flush()
        assert parser.stdout.readline().strip() == "ok"

        def command(identifier, action):
            now = int(time.time())
            data = {"id": identifier, "device_id": "deck", "action": action, "ts": now, "expires_at": now + 10}
            subscriber.publish("agent/codex/command", json.dumps(data), qos=1).wait_for_publish(2)
            return message("agentdeck/deck/ack", lambda d: d.get("id") == identifier)

        assert command("confirm-1", "confirm")["status"] == "completed"
        assert command("confirm-1", "confirm")["status"] == "completed"
        assert marker.read_text().splitlines() == ["confirm-1"]
        assert command("unsupported", "approve")["status"] == "rejected"
        broker.down(); wait_for(lambda: not bridge.connected.is_set())
        broker.start(); wait_for(bridge.connected.is_set, 15); wait_for(subscriber.is_connected, 15)
        message("pc/status", lambda p: p.get("online"), seconds=15)
        assert command("confirm-1", "confirm")["status"] == "completed"
        assert marker.read_text().splitlines() == ["confirm-1"]
        assert command("stop-1", "stop")["status"] == "completed"
        runner.wait(timeout=6)
        message("agent/codex/status", lambda p: not p.get("online"))
        bridge.stop.set(); thread.join(timeout=10)
        assert not thread.is_alive()
        print("PASS: real PC telemetry -> TCP MQTT -> production C++ parser; retained rejection; command/ack; dedup; broker restart/reconnect; real subprocess stop; graceful shutdown")
    finally:
        if bridge:
            bridge.stop.set()
        if thread:
            thread.join(timeout=10)
        if runner and runner.poll() is None:
            # Test-only cleanup covers exactly the process launched above.
            import psutil
            root = psutil.Process(runner.pid)
            for child in root.children(recursive=True):
                child.kill()
            runner.kill(); runner.wait(timeout=3)
        parser.stdin.close(); parser.wait(timeout=3); parser.stdout.close()
        subscriber.disconnect(); subscriber.loop_stop()
        broker.down(); broker.close()
        temporary.cleanup()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--broker":
        asyncio.run(broker_process(int(sys.argv[2])))
    else:
        main()
