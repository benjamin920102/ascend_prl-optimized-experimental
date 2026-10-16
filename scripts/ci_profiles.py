#!/usr/bin/env python3
"""Validate GitHub Actions optimization inputs and produce a safe build matrix.

Host-side only: a selected configuration is NOT equivalent to a verified PoW proof.
"""
import json
import os
from pathlib import Path

PRESETS = {
    "default": {"bufs": 4, "vec2x": 2, "backoff": 100, "basen": 128, "hit_epoch": 1},
    "baseline": {"bufs": 4, "vec2x": 2, "backoff": 100, "basen": 0, "hit_epoch": 0},
    "legacy": {"bufs": 4, "vec2x": 2, "backoff": 100, "basen": 128, "hit_epoch": 0},
    "epoch": {"bufs": 4, "vec2x": 2, "backoff": 100, "basen": 128, "hit_epoch": 1},
}


def _integer(value, name, minimum, maximum):
    # Reject expressions, negative values, whitespace and shell syntax.
    text = str(value)
    if not text.isascii() or not text.isdecimal():
        raise ValueError(f"{name} must be a decimal integer")
    number = int(text)
    if not minimum <= number <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return number


def validated(configuration):
    p = {
        "bufs": _integer(configuration["bufs"], "BUFS", 1, 16),
        "vec2x": _integer(configuration["vec2x"], "VEC2X", 1, 4),
        "backoff": _integer(configuration["backoff"], "BACKOFF", 0, 100000),
        "basen": _integer(configuration["basen"], "BASEN", 0, 1024),
        "hit_epoch": _integer(configuration["hit_epoch"], "HIT_EPOCH", 0, 1),
    }
    if p["bufs"] < p["vec2x"] or p["bufs"] % p["vec2x"]:
        raise ValueError("BUFS must be >= VEC2X and divisible by VEC2X")
    if p["basen"] and (p["basen"] % 32 or p["basen"] > 128):
        raise ValueError("BASEN must be 0 or a 32-aligned width <= 128")
    return p


def make_matrix(profile, custom=None):
    if profile == "matrix":
        names = ("baseline", "legacy", "epoch")
        configs = [(name, dict(PRESETS[name])) for name in names]
    elif profile == "custom":
        if custom is None:
            raise ValueError("custom profile requires all custom inputs")
        configs = [("custom", dict(custom))]
    elif profile in PRESETS:
        configs = [(profile, dict(PRESETS[profile]))]
    else:
        raise ValueError(f"unknown build profile: {profile!r}")
    return {"include": [dict(id=name, rank=128, **validated(params)) for name, params in configs]}


def main():
    custom = {
        "bufs": os.getenv("INPUT_CUSTOM_BUFS", "4"),
        "vec2x": os.getenv("INPUT_CUSTOM_VEC2X", "2"),
        "backoff": os.getenv("INPUT_CUSTOM_BACKOFF", "100"),
        "basen": os.getenv("INPUT_CUSTOM_BASEN", "128"),
        "hit_epoch": os.getenv("INPUT_CUSTOM_HIT_EPOCH", "true"),
    }
    # GitHub workflow_dispatch boolean inputs arrive as true/false strings.
    if custom["hit_epoch"] not in ("true", "false"):
        raise ValueError("INPUT_CUSTOM_HIT_EPOCH must be true or false")
    custom["hit_epoch"] = "1" if custom["hit_epoch"] == "true" else "0"
    plan = make_matrix(os.getenv("INPUT_PROFILE", "default"), custom)
    value = json.dumps(plan, separators=(",", ":"))
    print(json.dumps(plan, indent=2))
    out = os.getenv("GITHUB_OUTPUT")
    if out:
        with Path(out).open("a", encoding="utf-8") as f:
            f.write(f"matrix={value}\n")


if __name__ == "__main__":
    main()
