#!/usr/bin/env python3
"""Enforce centralized timer usage in the project.

The timer policy is:
- Runtime timer primitives must go through widgets/detail/timing_hub.
- Direct uses of QTimer and singleShot are forbidden outside allowlisted files.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Sequence


SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}
DEFAULT_SCAN_ROOTS = (
    Path("packages/ant_design_qt/src"),
    Path("examples/theme-demo"),
)
ALLOWLIST_FILES = {
    Path("packages/ant_design_qt/src/widgets/detail/timing_hub.cpp"),
    Path("packages/ant_design_qt/src/widgets/detail/timing_hub.h"),
}
PATTERNS = (
    re.compile(r"\bQTimer\b"),
    re.compile(r"\bsingleShot\s*\("),
)


@dataclass(frozen=True)
class Violation:
    file: Path
    line: int
    text: str


def iter_source_files(root: Path) -> Iterable[Path]:
    if root.is_file():
        if root.suffix.lower() in SOURCE_SUFFIXES:
            yield root
        return

    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        yield path


def scan_file(absolute_path: Path, relative_path: Path) -> List[Violation]:
    if relative_path in ALLOWLIST_FILES:
        return []

    violations: List[Violation] = []
    try:
        content = absolute_path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        content = absolute_path.read_text(encoding="utf-8", errors="ignore")

    for line_no, line in enumerate(content.splitlines(), start=1):
        for pattern in PATTERNS:
            if pattern.search(line):
                violations.append(Violation(file=relative_path, line=line_no, text=line.strip()))
                break
    return violations


def resolve_scan_paths(repo_root: Path, scan_roots: Sequence[str]) -> List[Path]:
    resolved: List[Path] = []
    for entry in scan_roots:
        path = Path(entry)
        absolute = path if path.is_absolute() else (repo_root / path)
        resolved.append(absolute.resolve())
    return resolved


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check timer usage policy for ant_design_qt.")
    parser.add_argument(
        "--scan-root",
        action="append",
        default=[],
        help="Relative or absolute path to scan. Can be provided multiple times.",
    )
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parents[1]
    scan_roots = args.scan_root if args.scan_root else [str(path) for path in DEFAULT_SCAN_ROOTS]
    resolved_roots = resolve_scan_paths(repo_root, scan_roots)

    violations: List[Violation] = []
    for root in resolved_roots:
        if not root.exists():
            continue
        for source in iter_source_files(root):
            rel = source.resolve().relative_to(repo_root.resolve())
            violations.extend(scan_file(source, rel))

    if violations:
        print("Timer policy violation(s) detected:")
        for violation in violations:
            print(f"- {violation.file}:{violation.line}: {violation.text}")
        return 1

    print("Timer policy check passed. No forbidden timer primitives were found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
