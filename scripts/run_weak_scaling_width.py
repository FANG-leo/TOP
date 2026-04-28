#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    base_script = script_dir / "run_weak_scaling.py"

    forwarded_args = sys.argv[1:]

    has_label = any(arg == "--label" or arg.startswith("--label=") for arg in forwarded_args)
    has_scale_axis = any(arg == "--scale-axis" or arg.startswith("--scale-axis=") for arg in forwarded_args)

    cmd = [sys.executable, str(base_script)]
    if not has_label:
        cmd.extend(["--label", "weak-width"])
    if not has_scale_axis:
        cmd.extend(["--scale-axis", "width"])
    cmd.extend(forwarded_args)

    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
