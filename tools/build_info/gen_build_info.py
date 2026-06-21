#!/usr/bin/env python3
"""Generate build-time firmware identity source files."""

from __future__ import annotations

import argparse
import datetime as dt
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("header", type=Path, help="output project_build_info.h path")
    parser.add_argument("source", type=Path, help="output project_build_info.c path")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_id = dt.datetime.now(dt.UTC).strftime("%Y%m%d%H%M%S")
    header = """#ifndef PROJECT_BUILD_INFO_H
#define PROJECT_BUILD_INFO_H

extern const char g_project_build_id[];

#endif
"""
    source = f"""#include "project_build_info.h"

const char g_project_build_id[] = "{build_id}";
"""

    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.source.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text(header, encoding="utf-8", newline="\n")
    args.source.write_text(source, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
