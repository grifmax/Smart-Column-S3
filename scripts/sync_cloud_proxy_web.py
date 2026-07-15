#!/usr/bin/env python3
"""
Sync cloud_proxy/web with the current frontend bundle from data/.

Copies plain assets from data/ to cloud_proxy/web and restores text bundles
from .gz artifacts when the plain file is not present in data/.
"""

from __future__ import annotations

import gzip
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
CLOUD_WEB_DIR = ROOT / "cloud_proxy" / "web"


def should_skip(path: Path) -> bool:
    return path.name.endswith(".gz")


def sync_plain_files() -> None:
    for src in DATA_DIR.rglob("*"):
        if not src.is_file() or should_skip(src):
            continue

        rel = src.relative_to(DATA_DIR)
        dst = CLOUD_WEB_DIR / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def restore_from_gzip(gzip_name: str, target_name: str) -> None:
    src = DATA_DIR / gzip_name
    if not src.exists():
        return

    dst = CLOUD_WEB_DIR / target_name
    dst.parent.mkdir(parents=True, exist_ok=True)
    with gzip.open(src, "rb") as gz_stream:
        dst.write_bytes(gz_stream.read())


def main() -> None:
    if not DATA_DIR.exists():
        raise SystemExit(f"Missing data directory: {DATA_DIR}")

    CLOUD_WEB_DIR.mkdir(parents=True, exist_ok=True)
    sync_plain_files()
    restore_from_gzip("app.js.gz", "app.js")


if __name__ == "__main__":
    main()
