"""Create local bridge configuration with real runner stop/cancel handlers."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--host", required=True)
parser.add_argument("--port", type=int, default=1883)
parser.add_argument("--device", default="agentdeck-01")
parser.add_argument("--output", type=Path, default=ROOT / "bridge/config.json")
args = parser.parse_args()
sys.path.insert(0, str(ROOT))
from bridge.service import IDENTIFIER
if not IDENTIFIER.fullmatch(args.device) or not 1 <= args.port <= 65535:
    parser.error("Invalid device ID or port")
cfg = json.loads((ROOT / "bridge/config.example.json").read_text(encoding="utf-8"))
state = args.output.resolve().parent / ".state"
cfg.update(host=args.host, port=args.port, devices=[args.device], state_dir=str(state))
handler = [sys.executable, "-m", "bridge.agent", "control", "--state-dir", str(state)]
cfg["handlers"] = {agent: {action: handler for action in ("stop", "cancel")} for agent in ("codex", "claude", "opencode")}
with args.output.open("x", encoding="utf-8") as file:
    json.dump(cfg, file, ensure_ascii=False, indent=2)
print(f"Created {args.output}. Start from repository root: python -m bridge.service --config {args.output}")
