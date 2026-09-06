#!/usr/bin/env python3
"""Build the checked-in antd manifest from synchronized upstream templates."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

THEMES = ("outlined", "filled", "twotone")
KEYWORDS = {"delete", "new", "signals", "slots", "emit", "foreach"}


def symbol_for(name: str) -> str:
    symbol = "".join(part[:1].upper() + part[1:] for part in re.findall(r"[A-Za-z0-9]+", name))
    if symbol[:1].isdigit() or symbol.lower() in KEYWORDS:
        symbol = "Icon" + symbol
    return symbol


def color_model(svg: str, theme: str) -> str:
    if theme != "twotone":
        return "monochrome"
    if re.search(r'data-adqt-slot=["\']tertiary["\']', svg, re.IGNORECASE):
        return "threeTone"
    return "twoTone"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    package = root / "packages" / "ant_design_icons_qt"
    templates = package / "resources" / "templates"
    custom = package / "resources" / "custom-icons"
    if custom.exists() and any(custom.rglob("*.svg")):
        raise SystemExit("custom icons are forbidden in the built-in antd generation inputs")

    entries = []
    for theme in THEMES:
        for source in sorted((templates / theme).glob("*.svg"), key=lambda item: item.name):
            name = source.stem
            entries.append({
                "variant": theme,
                "symbol": symbol_for(name),
                "name": name,
                "source": f"templates/{theme}/{source.name}",
                "colorModel": color_model(source.read_text(encoding="utf-8"), theme),
                "fit": "contain",
            })
    manifest = {
        "schemaVersion": 1,
        "pack": "antd",
        "cppNamespace": "adqt::icons::antd",
        "exportMacro": "ADQT_ICONS_EXPORT",
        "source": "ant-design/ant-design-icons pinned by upstream.lock.json",
        "entries": entries,
    }
    expected = json.dumps(manifest, indent=2, ensure_ascii=True) + "\n"
    output = package / "resources" / "antd.manifest.json"
    if args.check:
        actual = output.read_text(encoding="utf-8") if output.exists() else ""
        if actual != expected:
            raise SystemExit(f"stale generated manifest: {output}")
    else:
        output.write_text(expected, encoding="utf-8", newline="\n")
    print(f"antd manifest entries={len(entries)} check={args.check}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
