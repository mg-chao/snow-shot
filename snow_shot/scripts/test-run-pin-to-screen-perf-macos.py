#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offscreen regression tests for the macOS pin benchmark runner."""
from contextlib import contextmanager
import importlib.util
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, call, patch


def load_runner():
    path = Path(__file__).with_name("run-pin-to-screen-perf-macos.py")
    spec = importlib.util.spec_from_file_location("pin_benchmark", path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    return runner


@unittest.skipUnless(sys.platform == "darwin", "macOS benchmark")
class PinBenchmarkTests(unittest.TestCase):
    def test_clipboard_trial_targets_observed_display_and_uses_fixture_aspect(self):
        runner = load_runner()
        observer = Mock()
        responses = {"state": {"rss": 0, "windows": []},
                     "key": {"tick": 10}, "observe": {"window": 1, "ms": 5}}
        observer.call.side_effect = lambda op, **kwargs: responses.get(op, {})

        @contextmanager
        def running_app(*args):
            yield 123, None

        with TemporaryDirectory() as output:
            root = Path(output)
            args = SimpleNamespace(
                output=root, trace=False, baseline_env=[], width=900, height=900,
                startup=0, repeats=0, open_idle=0, between=0, idle=0)
            fixture = root / "fixture.png"
            with patch.multiple(runner, PROFILE_ROOT=root, running_app=running_app,
                                open_pin=lambda client: {"id": "pin"}, mutate=Mock()):
                runner.trial(args, observer, None, "baseline", "clipboard-png", 0,
                             (fixture, None, None), None, Mock())

        observer.call.assert_has_calls([
            call("clipboard", path=str(fixture), type="public.png"),
            call("hover"),
            call("key", code=runner.KEY["clipboard"]),
            call("observe", pid=123, tick=10, exclude=[], clear_overlay=False,
                 document=False, aspect_ratio=1.0),
        ])


if __name__ == "__main__":
    unittest.main()
