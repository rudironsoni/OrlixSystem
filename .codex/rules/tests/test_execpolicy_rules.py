#!/usr/bin/env python3
import json
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RULES = ROOT / ".codex" / "rules" / "orlix.rules"


def execpolicy_decision(command):
    result = subprocess.run(
        ["rtk", "codex", "execpolicy", "check", "--pretty", "--rules", str(RULES), "--", *command],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr or result.stdout)
    start = result.stdout.find("{")
    if start == -1:
        raise AssertionError(f"missing json output: {result.stdout!r}")
    return json.loads(result.stdout[start:])


class ExecPolicyRulesTests(unittest.TestCase):
    def assert_decision(self, command, expected):
        payload = execpolicy_decision(command)
        self.assertEqual(payload.get("decision"), expected, payload)

    def test_git_push_policy_matches_bare_and_rtk(self):
        self.assert_decision(["git", "push", "origin", "main"], "prompt")
        self.assert_decision(["rtk", "git", "push", "origin", "main"], "prompt")

    def test_destructive_remove_policy_matches_bare_and_rtk(self):
        self.assert_decision(["rm", "-rf", "Build"], "forbidden")
        self.assert_decision(["rtk", "rm", "-rf", "Build"], "forbidden")

    def test_expensive_make_policy_matches_bare_and_rtk(self):
        command = ["timeout", "18000", "make", "-f", "OrlixOS/Makefile", "test", "PROFILE=release"]
        self.assert_decision(command, "prompt")
        self.assert_decision(["rtk", *command], "prompt")

    def test_simctl_policy_matches_bare_and_rtk(self):
        self.assert_decision(["xcrun", "simctl", "shutdown", "all"], "prompt")
        self.assert_decision(["rtk", "xcrun", "simctl", "shutdown", "all"], "prompt")

    def test_xcodebuild_policy_matches_bare_and_rtk(self):
        command = [
            "xcodebuild",
            "-project",
            "OrlixSystem.xcodeproj",
            "-scheme",
            "OrlixTerminalProofDriverTests",
            "test",
        ]
        self.assert_decision(command, "prompt")
        self.assert_decision(["rtk", *command], "prompt")


if __name__ == "__main__":
    unittest.main()
