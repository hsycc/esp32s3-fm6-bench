#!/usr/bin/env python3
"""Stage the minimal Felucca FM6 sources required by the ESP32-S3 benchmark.

The benchmark intentionally does not vendor the whole Felucca firmware tree.
This script pins one upstream commit, initializes its submodules, generates the
fixed-point tables exactly as Felucca does, and copies only the FM6 core plus
its generated table header into components/fm6_port/upstream/.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = "https://github.com/hugelton/Felucca.git"
REVISION = "5738617968a22b5d4c0d3470cfcc95559052f446"

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "components" / "fm6_port" / "upstream"


def run(*args: str, cwd: Path | None = None) -> None:
    print("+", " ".join(args))
    subprocess.run(args, cwd=cwd, check=True)


def main() -> int:
    if shutil.which("git") is None:
        raise SystemExit("git is required")

    with tempfile.TemporaryDirectory(prefix="felucca-fm6-") as td:
        src = Path(td) / "Felucca"
        run("git", "clone", "--filter=blob:none", "--no-checkout", REPO, str(src))
        run("git", "checkout", REVISION, cwd=src)
        run("git", "submodule", "update", "--init", "--recursive", cwd=src)

        generated = Path(td) / "felucca_tables.h"
        run(sys.executable, "tools/gen_tables.py", str(generated), cwd=src)

        OUT.mkdir(parents=True, exist_ok=True)
        for old in OUT.iterdir():
            if old.is_file():
                old.unlink()
            elif old.is_dir():
                shutil.rmtree(old)

        shutil.copy2(src / "firmware" / "src" / "fm6_core.c", OUT / "fm6_core.c")
        shutil.copy2(generated, OUT / "felucca_tables.h")
        (OUT / "REVISION.txt").write_text(REVISION + "\n", encoding="utf-8")

    print(f"Staged Felucca FM6 revision {REVISION}")
    print(f"  {OUT / 'fm6_core.c'}")
    print(f"  {OUT / 'felucca_tables.h'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
