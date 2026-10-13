#!/usr/bin/env python3
"""Validate GitHub Actions optimization inputs and produce a safe build matrix.

Host-side only: a selected configuration is NOT equivalent to a verified PoW proof.
"""
import json
import os
from pathlib import Path

PRESETS = {
    "baseline": {"mbatch": 1, "bufs": 4, "vec2x": 2, "backoff": 2000, "basen": 0, "at_pack": 0},
    "mbatch2": {"mbatch": 2, "bufs": 2, "vec2x": 2, "backoff": 500, "basen": 128, "at_pack": 0},
    "atpack": {"mbatch": 2, "bufs": 2, "vec2x": 2, "backoff": 500, "basen": 128, "at_pack": 1},
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
        "mbatch": _integer(configuration["mbatch"], "MBATCH", 1, 8),
        "bufs": _integer(configuration["bufs"], "BUFS", 1, 16),
        "vec2x": _integer(configuration["vec2x"], "VEC2X", 1, 4),
        "backoff": _integer(configuration["backoff"], "BACKOFF", 0, 100000),
        "basen": _integer(configuration["basen"], "BASEN", 0, 1024),
        "at_pack": _integer(configuration["at_pack"], "AT_PACK", 0, 1),
    }
    if p["bufs"] < p["vec2x"] or p["bufs"] % p["vec2x"]:
        raise ValueError("BUFS must be >= VEC2X and divisible by VEC2X")
    if p["basen"] and (p["basen"] % 32 or p["basen"] > 128 * p["mbatch"]):
        raise ValueError("BASEN must be 0 or a 32-aligned width <= 128*MBATCH")
    return p


def make_matrix(profile, custom=None):
    if profile == "matrix":
        names = ("baseline", "mbatch2", "atpack")
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
        "mbatch": os.getenv("INPUT_CUSTOM_MBATCH", "2"),
        "bufs": os.getenv("INPUT_CUSTOM_BUFS", "2"),
        "vec2x": os.getenv("INPUT_CUSTOM_VEC2X", "2"),
        "backoff": os.getenv("INPUT_CUSTOM_BACKOFF", "500"),
        "basen": os.getenv("INPUT_CUSTOM_BASEN", "128"),
        "at_pack": os.getenv("INPUT_CUSTOM_AT_PACK", "false"),
    }
    # GitHub workflow_dispatch boolean inputs arrive as true/false strings.
    if custom["at_pack"] not in ("true", "false"):
        raise ValueError("INPUT_CUSTOM_AT_PACK must be true or false")
    custom["at_pack"] = "1" if custom["at_pack"] == "true" else "0"
    plan = make_matrix(os.getenv("INPUT_PROFILE", "mbatch2"), custom)
    value = json.dumps(plan, separators=(",", ":"))
    print(json.dumps(plan, indent=2))
    out = os.getenv("GITHUB_OUTPUT")
    if out:
        with Path(out).open("a", encoding="utf-8") as f:
            f.write(f"matrix={value}\n")


if __name__ == "__main__":
    main()
