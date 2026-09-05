"""Real task runner / event input. No demonstration data or generated counters."""
import argparse
import json
import shutil
import subprocess
import sys
import time

import psutil

from .sources import AGENTS, read_state, write_state


def control(directory, envelope):
    agent = envelope.get("agent")
    if agent not in AGENTS or envelope.get("action") not in ("stop", "cancel"):
        return 2
    now = time.time()
    if type(envelope.get("expires_at")) is not int or envelope["expires_at"] < now:
        return 2
    status, _ = read_state(directory, agent)
    if not status["online"]:
        return 3
    from pathlib import Path
    try:
        state = json.loads((Path(directory) / f"{agent}.json").read_text(encoding="utf-8"))
        if state.get("source") != "agentdeck-runner":
            return 3  # Never terminate an unrelated manually supplied process.
        process = psutil.Process(state["pid"])
        if abs(process.create_time() - state["process_started"]) > .01:
            return 3
        processes = process.children(recursive=True) + [process]
        for child in processes:
            try:
                child.terminate()
            except psutil.NoSuchProcess:
                pass
        _, alive = psutil.wait_procs(processes, timeout=1)
        for child in alive:
            child.kill()
        _, alive = psutil.wait_procs(alive, timeout=1)
        return 1 if alive else 0
    except (OSError, ValueError, KeyError, psutil.Error):
        return 1


def run(args):
    argv = args.command
    if argv and argv[0] == "--":
        argv = argv[1:]
    if not argv:
        raise ValueError("Supply a one-shot agent command after --")
    status, _ = read_state(args.state_dir, args.agent)
    if status["online"]:
        raise ValueError("This agent slot already has a live producer")
    argv[0] = shutil.which(argv[0]) or argv[0]
    process = subprocess.Popen(argv)
    identity = psutil.Process(process.pid).create_time()
    state = {"online": True, "working": True, "model": args.model, "task": args.task,
             "pid": process.pid, "process_started": identity, "source": "agentdeck-runner"}
    try:
        while process.poll() is None:
            write_state(args.state_dir, args.agent, dict(state, ts=int(time.time())))
            time.sleep(1)
        return process.returncode
    finally:
        if process.poll() is None:
            control(args.state_dir, {"agent": args.agent, "action": "stop", "expires_at": int(time.time()) + 10})
        write_state(args.state_dir, args.agent, {"online": False, "working": False, "ts": int(time.time()),
                    "task": args.task, "model": args.model, "completed_at": int(time.time()) if process.returncode == 0 else 0})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    runner = sub.add_parser("run", help="Wrap a real one-shot CLI task, with a 1s heartbeat")
    runner.add_argument("--state-dir", default="bridge/.state")
    runner.add_argument("--agent", choices=AGENTS, required=True)
    runner.add_argument("--task", required=True, help="Actual task label, not sent to the agent")
    runner.add_argument("--model", default="", help="Set only if this is the model actually used")
    runner.add_argument("command", nargs=argparse.REMAINDER)
    feed = sub.add_parser("feed", help="Accept real JSON state events on stdin (one object per line)")
    feed.add_argument("--state-dir", default="bridge/.state")
    feed.add_argument("--agent", choices=AGENTS, required=True)
    handler = sub.add_parser("control", help="Fixed MQTT handler for runner stop/cancel")
    handler.add_argument("--state-dir", default="bridge/.state")
    args = parser.parse_args()
    if args.mode == "run":
        return run(args)
    if args.mode == "control":
        return control(args.state_dir, json.loads(sys.stdin.read(1025)))
    for line in sys.stdin:
        if len(line.encode("utf-8")) > 4096:
            raise ValueError("Event exceeds 4096 bytes")
        event = json.loads(line)
        if not isinstance(event, dict) or type(event.get("online")) is not bool or type(event.get("working")) is not bool:
            raise ValueError("Each event requires actual online/working booleans")
        write_state(args.state_dir, args.agent, dict(event, ts=int(time.time())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
