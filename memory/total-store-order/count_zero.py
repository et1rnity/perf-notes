#!/usr/bin/env python3
"""Run tso-demo repeatedly and count the r0 == r1 == 0 outcome."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runs", type=int, nargs="?", default=10000)
    parser.add_argument(
        "--memory-model",
        choices=("tso", "sc"),
        default="tso",
        help="memory model passed to tso-demo (default: tso)",
    )
    parser.add_argument(
        "--binary", type=Path, default=Path(__file__).with_name("tso-demo")
    )
    args = parser.parse_args()
    if args.runs <= 0:
        parser.error("runs must be positive")

    binary = args.binary.resolve()
    hits = 0
    for index in range(1, args.runs + 1):
        try:
            completed = subprocess.run(
                [str(binary), args.memory_model],
                stdout=subprocess.PIPE,
                text=True,
                check=True,
            )
            result = json.loads(completed.stdout)
            r0, r1 = result["r0"], result["r1"]
            if r0 not in (0, 1) or r1 not in (0, 1):
                raise ValueError(f"unexpected result: {result!r}")
        except (OSError, subprocess.CalledProcessError, ValueError, KeyError, TypeError) as error:
            sys.exit(f"Run {index} failed: {error}; completed={index - 1}, hits={hits}")
        hits += r0 == 0 and r1 == 0

    print(f"memory-model={args.memory_model}")
    print(f"runs={args.runs}")
    print(f"r0=0,r1=0: {hits}")
    print(f"rate={hits / args.runs:.6%}")


if __name__ == "__main__":
    main()
