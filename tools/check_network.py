"""Compile the production protocol parser on the host, then test the bridge."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
build = ROOT / "build/host"
build.mkdir(parents=True, exist_ok=True)
exe = build / ("network.exe" if os.name == "nt" else "network")
subprocess.run([sys.executable, "-m", "ziglang", "c++", "-std=c++17", "-O1", "-Wall", "-Wextra",
                "-Isrc", "-I.pio/libdeps/agentdeck/ArduinoJson/src", "tests/network.cpp", "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_bridge.py", "-v"], check=True)
