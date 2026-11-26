#!/usr/bin/env python3
"""Materialize the annotated OCR captures and compare both detector policies."""
import argparse
import hashlib
import json
import math
import platform
import re
import statistics
import subprocess
import unicodedata
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


# Authored line transcriptions and locations. The renderer records the ink bounds
# of each annotated line before any recognition runs.
CAPTURES = [
    ("workspace", (640, 320), "Workspace", (
        "Recent captures", "Review desktop changes", "Version 2.4.1", "Capture complete",
        "Save to folder", "Copied to clipboard", "Resolution 640 × 320", "Local storage",
        "Account activity", "Session ready")),
    ("toolbar", (950, 136), "Capture controls", (
        "Select region", "Window capture", "Full screen", "Show cursor", "Copy image",
        "Save as PNG", "Clipboard ready", "Auto hide", "Capture timer", "Elapsed 0:03")),
    ("sidebar", (136, 950), "Projects", (
        "Workspace", "New note", "Dashboard", "Media", "Exports", "Screenshots",
        "Favorites", "Settings", "Help center", "Sign out")),
    ("desktop", (1920, 1080), "System Settings", (
        "Display resolution", "Refresh rate 60 Hz", "Night lighting schedule",
        "Use hardware acceleration", "System language", "Connected displays",
        "Device scale 100%", "Graphics preferences", "Advanced settings", "Apply changes")),
    ("notifications", (660, 400), "Notifications", (
        "Download finished", "Screenshot saved", "New version available", "Focus session",
        "Cloud backup paused", "Updates installed", "Storage quota", "Today at 08:45",
        "Network connected", "Dismiss all")),
    ("terminal", (980, 160), "Build output", (
        "Build started", "Loading defaults", "Compiler ready", "Release profile",
        "Updating assets", "Target x64", "Revision 6af3", "Link complete", "Elapsed 3.2 s",
        "Exit code 0")),
    ("settings-list", (146, 824), "Settings", (
        "General", "Appearance", "Shortcuts", "Storage", "Network", "Privacy",
        "Language", "Updates", "Plugins", "About")),
    ("schedule", (800, 420), "This week", (
        "Monday review", "Team checkpoint", "Office hours", "Design notes", "Build release",
        "Validate package", "Translate strings", "Project update", "Morning session",
        "Progress saved")),
    ("note", (725, 460), "工作记录", (
        "截图已经保存", "Review open changes", "识别文字", "复制到剪贴板", "Local OCR",
        "文件列表", "显示设置", "Document preview", "测试完成", "Export report")),
    ("dark-theme", (864, 480), "Appearance", (
        "Color theme", "High contrast", "Follow system", "Night mode", "Accent color",
        "Window opacity", "Preview mode", "Restore defaults", "Font settings", "Sample text")),
    ("review", (1020, 520), "Document review", (
        "Chapter 1 Overview", "OCR strategy", "Native screenshot pixels", "Image border test",
        "Original coordinates", "Review source text", "Page 4 of 12", "Annotations saved",
        "Copy selection", "Close preview")),
    ("dense", (780, 570), "Activity monitor", (
        "CPU utilization", "GPU allocation", "Process memory", "Requests completed",
        "Queue duration", "Working directory", "Runtime 1.5.2", "Text recognition",
        "Capture history", "Disk usage")),
]

PERFORMANCE = {"workspace", "toolbar", "sidebar", "desktop"}
HEIGHTS = ("<10", "10–15", "16–23", "≥24")
VARIANTS = ("baseline", "native")


def make_corpus(destination):
    destination.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, (w, h), title, labels in CAPTURES:
        dark = name in {"terminal", "dark-theme", "dense"}
        base = (20, 25, 35) if dark else (248, 249, 252)
        header = (36, 46, 61) if dark else (228, 234, 246)
        color = (229, 236, 248) if dark else (23, 31, 46)
        muted = (98, 108, 123) if dark else (153, 158, 165)
        im = Image.new("RGB", (w, h), base)
        draw = ImageDraw.Draw(im)
        draw.rectangle((0, 0, w, 38), fill=header)
        lines = []

        def line(text, size, x, y, tags=frozenset()):
            family = "msyh.ttc" if any(ord(c) >= 0x2e80 for c in text) else "segoeui.ttf"
            font = ImageFont.truetype(family, size)
            left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
            tw, th = right - left, bottom - top
            if "edge" in tags:
                x, y = w - tw, h - th
            assert 0 <= x <= w - tw and 0 <= y <= h - th, (name, text, x, y, tw, th)
            draw.text((x - left, y - top), text, font=font,
                      fill=muted if "low-contrast" in tags else color)
            box = [max(0, x - 1), max(0, y - 1), min(w, x + tw + 1), min(h, y + th + 1)]
            tags = sorted(tags | ({"small"} if box[3] - box[1] < 16 else set()))
            lines.append({"text": text, "box": box, "tags": tags})

        line(title, 19 if w < 200 else 30, 10, 5)
        for i, text in enumerate(labels):
            tags = set()
            if i in {2, 7}:
                tags.add("low-contrast")
            if i == 9:
                tags.add("edge")
            if w < 200:
                size = (10, 14, 16, 19, 22)[i % 5]
                x, y = 8, 63 + i * 63
            elif h <= 160:
                size = (8, 14, 19, 26, 15, 11, 19, 28, 12, 22)[i]
                x, y = 12 + (i % 5) * (w // 5), 60 + (i // 5) * 35
            else:
                size = (8, 11, 14, 17, 21, 26, 32, 16, 23, 24)[i]
                x, y = 18 + (i % 2) * (w // 2), 57 + (i // 2) * ((h - 90) // 5)
                if i % 2 == 0:
                    draw.line((12, y + 39, w - 12, y + 39), fill=header)
            line(text, size, x, y, tags)
        bgr = im.tobytes("raw", "BGR")
        im.save(destination / f"{name}.png")
        (destination / f"{name}.bgr").write_bytes(bgr)
        rows.append({"name": name, "size": [w, h], "sha256_bgr": hashlib.sha256(bgr).hexdigest(),
                     "lines": lines})
    (destination / "annotations.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2),
                                                    encoding="utf-8")
    return rows


def polygon_area(poly):
    return abs(sum(x * v - y * u for (x, y), (u, v) in zip(poly, poly[1:] + poly[:1]))) / 2


def intersection_over_union(poly, box):
    clipped = poly
    for axis, bound, side in [(0, box[0], 1), (1, box[1], 1),
                              (0, box[2], -1), (1, box[3], -1)]:
        if not clipped:
            return 0.0
        vertices = clipped
        clipped = []
        for a, b in zip(vertices[-1:] + vertices[:-1], vertices):
            in_a = (a[axis] - bound) * side >= 0
            in_b = (b[axis] - bound) * side >= 0
            if in_a != in_b:
                t = (bound - a[axis]) / (b[axis] - a[axis])
                clipped.append([p + t * (q - p) for p, q in zip(a, b)])
            if in_b:
                clipped.append(b)
    overlap = polygon_area(clipped)
    union = polygon_area(poly) + (box[2] - box[0]) * (box[3] - box[1]) - overlap
    return overlap / union if union > 0 else 0.0


def match_lines(predictions, references):
    choices = []
    for ref in references:
        ranked = [(intersection_over_union(p["points"], ref["box"]), i)
                  for i, p in enumerate(predictions)]
        choices.append([i for iou, i in sorted(ranked, reverse=True) if iou >= 0.5])
    matched = {}

    def assign(ref, seen):
        for i in choices[ref]:
            if i in seen:
                continue
            seen.add(i)
            if i not in matched or assign(matched[i], seen):
                matched[i] = ref
                return True
        return False

    for ref in range(len(references)):
        assign(ref, set())
    return matched


def height_group(height):
    return next(label for top, label in zip((10, 16, 24, math.inf), HEIGHTS) if height < top)


def normalize(text):
    return re.sub(r"\s+", " ", unicodedata.normalize("NFC", text).strip())


def edits(reference, estimate):
    previous = list(range(len(estimate) + 1))
    for i, char in enumerate(reference, 1):
        current = [i]
        for j, other in enumerate(estimate, 1):
            current.append(min(previous[j] + 1, current[-1] + 1,
                               previous[j - 1] + (char != other)))
        previous = current
    return previous[-1]


def summarize_quality(captures, records, backend, policy):
    counters = {name: defaultdict(int) for name in (*HEIGHTS, "all")}
    missed = []
    for capture in captures:
        name, references = capture["name"], capture["lines"]
        detections = records[backend, policy, name, "detect"]["result"]["boxes"]
        recognized = records[backend, policy, name, "ocr"]["result"]
        mapping = match_lines(detections, references)
        detected_refs = set(mapping.values())
        recognized_map = {j: i for i, j in match_lines(recognized, references).items()}
        for i, ref in enumerate(references):
            group = height_group(ref["box"][3] - ref["box"][1])
            detection_found = i in detected_refs
            match = recognized_map.get(i)
            text = normalize(ref["text"])
            hypothesis = "" if match is None else normalize(recognized[match]["text"])
            for key in (group, "all"):
                counts = counters[key]
                counts["reference"] += 1
                counts["matched"] += detection_found
                counts["characters"] += len(text)
                counts["edits"] += edits(text, hypothesis)
                if detection_found:
                    counts["predicted"] += 1
            if not detection_found:
                best = max((intersection_over_union(p["points"], ref["box"])
                            for p in detections), default=0.0)
                missed.append({"capture": name, "height": group, "best_iou": round(best, 3), **ref})
        for i, p in enumerate(detections):
            if i not in mapping:
                ys = [y for _, y in p["points"]]
                for key in (height_group(max(ys) - min(ys)), "all"):
                    counters[key]["unmatched"] += 1
                    counters[key]["predicted"] += 1
    return {"counts": counters, "missed": missed}


def run(args, captures):
    model = args.models
    chosen = [entry for entry in captures if entry["name"] in PERFORMANCE or not args.performance_only]
    timeout = args.timeout
    records = {}
    journal = args.output / "measurements.jsonl"
    journal.write_text("", encoding="utf-8")
    for backend in args.backends:
        for index, capture in enumerate(chosen):
            name = capture["name"]
            w, h = capture["size"]
            for stage in ("detect", "ocr"):
                for policy in VARIANTS[::(-1 if index % 2 else 1)]:
                    repeats = args.repeats if name in PERFORMANCE else 1
                    command = [
                        args.worker, "--compare-sizing", policy, stage, backend, "accuracy",
                        model / "PP-OCRv6_det_small.onnx", model / "PP-OCRv6_rec_small.onnx",
                        model / "ppocrv6_dict.txt", f"{w}x{h}", args.output / f"{name}.bgr",
                        str(repeats),
                    ]
                    result = subprocess.run(command, capture_output=True, check=False, timeout=timeout)
                    if result.returncode:
                        raise RuntimeError(f"{backend} {policy} {name} {stage}: "
                                           f"{result.stderr.decode(errors='replace')}")
                    values = json.loads(result.stdout)
                    values["p50_ms"] = statistics.median(values["ms"])
                    values["p95_ms"] = sorted(values["ms"])[math.ceil(0.95 * repeats) - 1]
                    records[backend, policy, name, stage] = values
                    with journal.open("a", encoding="utf-8") as file:
                        file.write(json.dumps({
                            "backend": backend, "policy": policy, "capture": name,
                            "stage": stage, **values,
                        }, ensure_ascii=False) + "\n")
                    print(backend, name, stage, policy, f"{values['p50_ms']:.2f} ms", flush=True)
    quality = {}
    if not args.performance_only:
        for backend in args.backends:
            quality[backend] = {policy: summarize_quality(chosen, records, backend, policy)
                                for policy in VARIANTS}
    (args.output / "results.json").write_text(json.dumps({
        "host": platform.platform(), "processor": platform.processor(), "model": "Small V6",
        "runs_per_performance_case": args.repeats, "warmup_per_case": 1,
        "cases": [{"backend": k[0], "policy": k[1], "capture": k[2], "stage": k[3], **v}
                  for k, v in records.items()], "quality": quality,
    }, ensure_ascii=False, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--models", type=Path)
    parser.add_argument("--backends", choices=("cpu", "gpu"), default=("cpu", "gpu"), nargs="+")
    parser.add_argument("--repeats", type=int, default=30)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--performance-only", action="store_true")
    args = parser.parse_args()
    captures = make_corpus(args.output)
    if args.worker is not None:
        if args.models is None:
            parser.error("--worker requires --models")
        run(args, captures)


if __name__ == "__main__":
    main()
