#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure real pin commands against a native ScreenCaptureKit pixel observer."""
import argparse
from contextlib import contextmanager, suppress
import ctypes
import json
import math
import os
from pathlib import Path
import random
import shutil
import statistics
import struct
import subprocess
import sys
import time
import uuid
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from mcp_fixture_tests import Client
from mcp_application_fixture_tests import mutate_pin

PALETTE = ((232, 48, 121), (39, 212, 201), (254, 194, 45), (103, 76, 223))
KEY = {"capture": 25, "selection": 28, "clipboard": 26, "restore": 23}
SCENARIOS = ("clipboard-png", "clipboard-tiff", "clipboard-animation", "clipboard-file",
             "clipboard-text", "clipboard-html", "mcp-file", "selection", "selection-quick",
             "history", "mcp-selection", "mcp-document", "restore")
PROFILE_ROOT = Path.home() / "Library/Application Support/SnowShot"
mach_tick = ctypes.CDLL("/usr/lib/libSystem.B.dylib").mach_absolute_time
mach_tick.restype = ctypes.c_uint64


def until(check, label, seconds=5.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.015)
    raise RuntimeError(f"Timed out waiting for {label}")


def png(path, width, height, animate=False):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    jitter = iter(random.Random(19).randbytes(width * height))
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            color = PALETTE[2 * (y >= height // 2) + (x >= width // 2)]
            delta = next(jitter) % 21 - 10
            row.extend(max(0, min(255, channel + delta)) for channel in color)
        rows.append(b"\0" + row)
    header = chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    first_frame = chunk(b"IDAT", zlib.compress(b"".join(rows), 2))
    if animate:
        def frame_control(index):
            return chunk(b"fcTL", struct.pack(">IIIIIHHBB", index, width, height, 0, 0,
                                             5, 1, 0, 0))

        second_frame = struct.pack(">I", 2) + zlib.compress(b"".join(reversed(rows)), 2)
        first_frame = (chunk(b"acTL", struct.pack(">II", 2, 0)) + frame_control(0) +
                       first_frame + frame_control(1) + chunk(b"fdAT", second_frame))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + header + first_frame + chunk(b"IEND", b""))


class Observer:
    def __init__(self, program, log):
        self.proc = subprocess.Popen([program], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=log, text=True)
        if not self.read().get("ready"):
            raise RuntimeError("The compositor observer did not initialize")

    def read(self):
        line = self.proc.stdout.readline()
        if not line:
            raise RuntimeError("The compositor observer stopped")
        result = json.loads(line)
        if "error" in result:
            raise RuntimeError(result["error"])
        return result

    def call(self, op, **fields):
        self.proc.stdin.write(json.dumps(dict(op=op, **fields), separators=(",", ":")) + "\n")
        self.proc.stdin.flush()
        return self.read()

    def close(self):
        if self.proc.poll() is None:
            self.proc.stdin.write('{"op":"quit"}\n')
            self.proc.stdin.close()
            self.proc.wait(timeout=5)


def config(profile, scenario):
    profile.mkdir(parents=True)
    settings = {
        "storage": {"schema_version": 11},
        "updates": {"mode": "manual"},
        "mcp": {"enabled": True},
        "interface": {"language": "en_US"},
        "text_recognition": {"resident_process": False, "model_hot_start": False},
        "screenshot_selection": {"smart_selection": "disabled"},
        "global_shortcuts": {
            "screenshot": ["Meta+Alt+Shift+9"],
            "screenshot_fixed": ["Meta+Alt+Shift+8"],
            "pin_clipboard_content": ["Meta+Alt+Shift+7"],
            "restore_last_closed_windows": ["Meta+Alt+Shift+5"],
            "screenshot_copy": [],
            "screen_record": [],
        },
        "pin_to_screen": {"automatic_text_recognition": False,
                          "text_color": "#161616ff", "text_background_color": "#f4edd8ff",
                          "duplicate_content_action": "repeat_action"},
    }
    if scenario == "clipboard-text":
        settings["pin_to_screen"].update(text_color="#f4edd8ff", text_background_color="#181818ff")
    elif scenario == "clipboard-html":
        settings["interface"]["theme_mode"] = "light"
    (profile / "config.json").write_text(json.dumps(settings))


def pins(client):
    return client.tool("snow_shot_pinned_list", {"limit": 100})["result"]["windows"]


def mutate(client, pin, action):
    return mutate_pin(client, pin, "snow_shot_pinned_action", action=action)["result"]


def invoke(observer, client, pid, scenario, fixture, text, html, history_id=None):
    if scenario.startswith("clipboard-"):
        sources = {
            "clipboard-png": (fixture, "public.png"),
            "clipboard-tiff": (fixture.with_suffix(".tiff"), "public.tiff"),
            "clipboard-animation": (fixture.with_suffix(".apng"), "public.png"),
            "clipboard-file": (fixture, "file"),
            "clipboard-text": (text, "public.utf8-plain-text"),
            "clipboard-html": (html, "public.html"),
        }
        path, kind = sources[scenario]
        observer.call("clipboard", path=str(path), type=kind)
        observer.call("hover")
        return observer.call("key", code=KEY["clipboard"])["tick"]
    if scenario == "mcp-file":
        start = mach_tick()
        result = client.tool("snow_shot_pinned_create",
                             {"source": {"kind": "file", "path": str(fixture)}})["result"]
        if not (result["ready"] and result["visible"]):
            raise RuntimeError("MCP pin creation did not return ready and visible")
        return start
    if scenario == "mcp-document":
        opened = client.tool("snow_shot_document_open", {"path": str(fixture)})
        document = {"document_id": opened["result"]["document_id"],
                    "expected_revision": opened["revision"]}
        start = mach_tick()
        client.tool("snow_shot_document_pin", document)
        client.tool("snow_shot_document_close", document)
        return start
    if scenario == "restore":
        return observer.call("key", code=KEY["restore"])["tick"]
    if scenario == "history":
        start = mach_tick()
        client.tool("snow_shot_history_action", {"history_id": history_id, "action": "pin"})
        return start
    if scenario == "mcp-selection":
        rect = observer.call("fixture", show=True)["selection"]
        observer.call("hover")
        session = client.tool("snow_shot_screenshot_begin", {
            "presentation": "silent", "target": "current_monitor", "capture_cursor": False
        })["result"]
        selection = client.tool("snow_shot_screenshot_set_selection", {
            "session_id": session["session_id"], "expected_revision": session["revision"],
            "type": "rectangle", "bounds": [int(value) for value in rect]
        })["result"]
        observer.call("fixture", show=False)
        start = mach_tick()
        result = client.tool("snow_shot_screenshot_pin", {
            "session_id": session["session_id"], "expected_revision": selection["revision"]
        })["result"]
        if not result["pinned"]:
            raise RuntimeError("MCP screenshot pin did not complete")
        client.tool("snow_shot_screenshot_cancel", {"session_id": session["session_id"]})
        return start

    observer.call("fixture", show=True)
    observer.call("hover")
    quick = scenario == "selection-quick"
    observer.call("key", code=KEY["selection" if quick else "capture"])
    observer.call("overlay", pid=pid)
    time.sleep(.09)
    released = observer.call("select", hide=quick)["tick"]
    if quick:
        return released
    time.sleep(.045)
    observer.call("fixture", show=False)
    return observer.call("key", code=KEY["selection"])["tick"]


@contextmanager
def running_app(executable, identifier, descriptor, app_log, settings, trace=None):
    descriptor.unlink(missing_ok=True)
    environment = dict(os.environ, SNOW_SHOT_MCP_DESCRIPTOR=str(descriptor))
    if trace:
        trace.unlink(missing_ok=True)
        environment["SNOW_SHOT_PIN_PERF_TRACE"] = str(trace)
    else:
        environment.pop("SNOW_SHOT_PIN_PERF_TRACE", None)
    for setting in settings:
        key, value = setting.split("=", 1)
        environment[key] = value
    client = None
    child = subprocess.Popen([executable, "--autostart", f"--e2e-instance-id={identifier}"],
                             env=environment, stdout=app_log, stderr=subprocess.STDOUT)
    try:
        until(descriptor.is_file, "application readiness", 12)
        client = Client(executable.with_name("snow-shot-mcp"), descriptor)
        yield child.pid, client
    finally:
        if client is not None:
            with suppress(AssertionError):
                client.tool("snow_shot_app_action", {"action": "quit"})
            with suppress(AssertionError):
                client.close()
        try:
            child.wait(timeout=2)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=2)
        descriptor.unlink(missing_ok=True)


def open_pin(client):
    return until(lambda: next((pin for pin in pins(client) if pin["open"]), None),
                 "published pin record")


def trial(args, observer, executable, arm, scenario, launch, paths, app_log, rows):
    identifier = uuid.uuid4().hex
    profile = PROFILE_ROOT / ("snow_shot-e2e-" + identifier)
    descriptor = args.output / f"endpoint-{identifier}.json"
    trace = args.output / f"trace-{scenario}-{arm}-{launch}.jsonl" if args.trace else None
    settings = args.compare_env if arm == "candidate" else args.baseline_env
    document = scenario in ("clipboard-text", "clipboard-html")
    file_fixture = ((scenario.startswith("clipboard-") and not document) or
                    scenario in ("mcp-file", "mcp-document", "restore"))
    observation = dict(clear_overlay=scenario in ("selection", "selection-quick"),
                       document=document)
    if file_fixture:
        observation["aspect_ratio"] = args.width / args.height
    config(profile, scenario)
    history_id = None
    try:
        observer.call("fixture", show=False)
        if scenario in ("restore", "history"):
            with running_app(executable, identifier, descriptor, app_log, settings) as (pid, client):
                if scenario == "restore":
                    created = client.tool("snow_shot_pinned_create", {
                        "source": {"kind": "file", "path": str(paths[0])}
                    })["result"]
                    mutate(client, created["id"], "close")
                    until(lambda: not any(pin["open"] for pin in pins(client)), "seed close")
                    time.sleep(args.between)
                else:
                    time.sleep(args.startup)
                    windows = observer.call("state", pid=pid)["windows"]
                    start = invoke(observer, client, pid, "selection", *paths)
                    observer.call("observe", pid=pid, tick=start, exclude=windows, clear_overlay=True)
                    records = until(lambda: client.tool("snow_shot_history_list", {
                        "limit": 1})["result"]["records"], "saved capture history")
                    history_id = records[0]["history_id"]
                    mutate(client, open_pin(client)["id"], "destroy")
        with running_app(executable, identifier, descriptor, app_log, settings, trace) as (pid, client):
            time.sleep(args.startup)
            idle_before = observer.call("state", pid=pid)["rss"]
            idle_open = None
            for sample in range(args.repeats + 1):
                # Vary the trigger phase relative to display refresh equally in both arms.
                time.sleep(((launch * 17 + sample * 11) % 31) / 1000)
                excluded = observer.call("state", pid=pid)["windows"]
                start = invoke(observer, client, pid, scenario, *paths, history_id=history_id)
                frame = observer.call(
                    "observe", pid=pid, tick=start, exclude=excluded, **observation)
                rows(dict(kind="frame", arm=arm, scenario=scenario, launch=launch, sample=sample,
                          trigger_tick=start, **frame))
                if args.open_idle and sample == args.repeats:
                    time.sleep(args.open_idle)
                    idle_open = observer.call("state", pid=pid)["rss"]
                mutate(client, open_pin(client)["id"],
                       "close" if scenario == "restore" else "destroy")
                until(lambda: frame["window"] not in observer.call("state", pid=pid)["windows"],
                      "pin dismissal")
                time.sleep(args.between)
            time.sleep(args.idle)
            idle_after = observer.call("state", pid=pid)["rss"]
            rows(dict(kind="memory", arm=arm, scenario=scenario, launch=launch,
                      idle_before=idle_before, idle_open=idle_open, idle_after=idle_after))
    finally:
        shutil.rmtree(profile)


def percentiles(values):
    return [round(statistics.median(values), 3),
            round(sorted(values)[math.ceil(len(values) * .95) - 1], 3)]


def summarize(rows):
    result = {}
    for scenario, arm in sorted({(r["scenario"], r["arm"]) for r in rows}):
        selected = [r for r in rows if r["scenario"] == scenario and r["arm"] == arm]
        first = [r["ms"] for r in selected if r["kind"] == "frame" and r["sample"] == 0]
        warm = [r["ms"] for r in selected if r["kind"] == "frame" and r["sample"] > 0]
        memory = [r for r in selected if r["kind"] == "memory"]
        open_memory = [r["idle_open"] for r in memory if r.get("idle_open") is not None]
        result.setdefault(scenario, {})[arm] = dict(
            first_n=len(first), first_ms=percentiles(first),
            repeated_n=len(warm), repeated_ms=percentiles(warm) if warm else None,
            idle_before_mib=round(statistics.median(r["idle_before"] for r in memory) / 2**20, 3),
            idle_after_mib=round(statistics.median(r["idle_after"] for r in memory) / 2**20, 3),
            idle_open_mib=round(statistics.median(open_memory) / 2**20, 3) if open_memory else None)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--compare-app", type=Path)
    parser.add_argument("--compare-env", default=[], action="append")
    parser.add_argument("--baseline-env", default=[], action="append")
    parser.add_argument("--observer", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", action="append", choices=SCENARIOS)
    parser.add_argument("--launches", type=int, default=24, help="Fresh processes per arm and route")
    parser.add_argument("--repeats", type=int, default=3, help="Additional pins per process")
    parser.add_argument("--width", type=int, default=1200)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--startup", type=float, default=.75)
    parser.add_argument("--between", type=float, default=.18)
    parser.add_argument("--idle", type=float, default=.8)
    parser.add_argument("--open-idle", type=float, default=0,
                        help="Idle interval before sampling RSS with the last pin open")
    parser.add_argument("--trace", action="store_true")
    args = parser.parse_args()
    if (args.launches <= 0 or args.repeats < 0 or args.width <= 0 or args.height <= 0 or
            min(args.startup, args.between, args.idle, args.open_idle) < 0):
        parser.error("Expected positive sample and fixture sizes and nonnegative intervals")
    if any("=" not in setting for setting in args.compare_env + args.baseline_env):
        parser.error("Trial environment settings must use NAME=VALUE")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    summary_path = args.output / "summary.json"
    summary_path.unlink(missing_ok=True)
    scenarios = args.scenario or SCENARIOS
    arms = ("baseline", "candidate") if args.compare_app or args.compare_env else ("baseline",)
    executables = {"baseline": args.app.resolve(),
                   "candidate": (args.compare_app or args.app).resolve()}
    fixture, text, html = (args.output / f"fixture.{ext}" for ext in ("png", "txt", "html"))
    png(fixture, args.width, args.height)
    if "clipboard-animation" in scenarios:
        png(fixture.with_suffix(".apng"), args.width, args.height, animate=True)
    if "clipboard-tiff" in scenarios:
        subprocess.run(["/usr/bin/sips", "-s", "format", "tiff", fixture, "--out",
                        fixture.with_suffix(".tiff")], stdout=subprocess.DEVNULL, check=True)
    text.write_text(("████████  ▇▇▇▇▇▇  ████████\n" * 14), encoding="utf-8")
    html.write_text("<html><body style='background:#f4edd8;color:#161616'>" +
                    ("<p><b>████████  ▇▇▇▇▇▇  ████████</b></p>" * 14) +
                    "</body></html>", encoding="utf-8")
    with (args.output / "observer.log").open("w") as observer_log, \
            (args.output / "app.log").open("w") as app_log, \
            (args.output / "samples.jsonl").open("w") as samples:
        observer = Observer(args.observer.resolve(), observer_log)
        rows = []

        def record(row):
            rows.append(row)
            print(json.dumps(row, separators=(",", ":")), file=samples, flush=True)

        try:
            for scenario in scenarios:
                for launch in range(args.launches):
                    order = arms[::1 if launch % 2 == 0 else -1]
                    for arm in order:
                        trial(args, observer, executables[arm], arm, scenario, launch,
                              (fixture, text, html), app_log, record)
                    if (launch + 1) % 8 == 0 or launch + 1 == args.launches:
                        print(f"{scenario} {launch + 1}/{args.launches}", flush=True)
        finally:
            observer.close()
    summary = json.dumps(summarize(rows), indent=2, sort_keys=True) + "\n"
    summary_path.write_text(summary)
    print(summary, end="")


if __name__ == "__main__":
    main()
