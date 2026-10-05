#!/usr/bin/env python3
"""Same thing as main.rs, but slower and with opinions."""
import sys

WEIGHTS = {"blur": 5, "bezier": 8, "gaps": 3}


def rice_level(text: str) -> int:
    total = 0
    for line in text.splitlines():
        if line.lstrip().startswith("#"):
            continue
        total += next((w for k, w in WEIGHTS.items() if k in line), 1)
    return total


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "hyprland.conf"
    print(f"{path}: rice level {rice_level(open(path).read())}")
