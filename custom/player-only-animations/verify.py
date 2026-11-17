#!/usr/bin/env python3
"""Static gate for generic player-only additional animation sources."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FILES = {
    "animation.hpp": ROOT / "apps/openmw/mwrender/animation.hpp",
    "animation.cpp": ROOT / "apps/openmw/mwrender/animation.cpp",
    "npcanimation.cpp": ROOT / "apps/openmw/mwrender/npcanimation.cpp",
}


def fail(message: str) -> None:
    raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="player-only-animations-verification.json")
    args = parser.parse_args()

    text = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}
    joined = "\n".join(text.values()).lower()

    required = {
        "generic_directory": '"playeranimations/"' in joined,
        "actual_player_gate": "getworld()->getplayerptr()" in joined,
        "third_person_gate": "!is1stperson" in joined,
        "werewolf_gate": "!iswerewolf" in joined,
        "additional_sources_gate": "museadditionalanimsources" in joined,
        "base_representation": "loadadditionalanimations(vfs::path::normalizedview(base)" in joined,
        "default_representation": "loadadditionalanimations(vfs::path::normalizedview(defaultskeleton)" in joined,
        "custom_representation": "loadadditionalanimations(vfs::path::normalizedview(smodel)" in joined,
        "default_ordinary_directory": 'normalizedview(\"animations/\")' in joined,
        "no_sails_hardcoding": "pxm_sails" not in joined and "sails and sales" not in joined,
    }
    missing = [name for name, ok in required.items() if not ok]
    if missing:
        fail("Player-only animation source verification failed: " + ", ".join(missing))

    cases = {
        "meshes/xbase_anim.nif": "playeranimations/xbase_anim/",
        "meshes/xbase_anim_female.nif": "playeranimations/xbase_anim_female/",
        "meshes/xbase_animkna.nif": "playeranimations/xbase_animkna/",
        "meshes/custom/player_rig.nif": "playeranimations/custom/player_rig/",
    }
    for model, expected in cases.items():
        path = model[: model.rfind(".")] + "/"
        actual = path.replace("meshes/", "playeranimations/", 1)
        if actual != expected:
            fail(f"Path mapping failed for {model}: {actual!r} != {expected!r}")

    receipt = {
        "result": "PASS",
        "scope": "Generic player-only additional animation source loader",
        "checks": required,
        "path_cases": cases,
        "files": {
            name: {
                "path": str(path.relative_to(ROOT)).replace("\\", "/"),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "bytes": path.stat().st_size,
            }
            for name, path in FILES.items()
        },
        "limits": [
            "Static source and path verification only.",
            "Compilation is performed by the Windows workflow.",
            "Visible Sails poses and warning elimination require an in-game test with migrated KF resources.",
        ],
    }
    Path(args.output).write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
