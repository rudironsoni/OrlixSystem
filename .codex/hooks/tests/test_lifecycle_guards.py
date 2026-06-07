#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HOOK_DIR = Path(__file__).resolve().parents[1]
POST_TOOL_REVIEW = HOOK_DIR / "post_tool_use_review.py"
STOP_CLAIM_CHECK = HOOK_DIR / "stop_claim_check.py"
COMPACT_PLAN_CHECK = HOOK_DIR / "compact_plan_check.py"


def run_hook(script, payload, cwd=None):
    return subprocess.run(
        [sys.executable, str(script)],
        input=json.dumps(payload),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=cwd,
        check=False,
    )


class LifecycleGuardTests(unittest.TestCase):
    def test_post_tool_review_warns_on_failure_output(self):
        result = run_hook(
            POST_TOOL_REVIEW,
            {
                "hook_event_name": "PostToolUse",
                "tool_name": "Bash",
                "tool_response": "ORLIX-COREUTILS-TEST-END failures=1 skips=0",
            },
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("ORLIX-HARNESS-WARN", result.stderr)

    def test_stop_claim_check_warns_without_active_plan(self):
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(["git", "init"], cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)

            result = run_hook(
                STOP_CLAIM_CHECK,
                {"hook_event_name": "Stop", "response": "This is complete."},
                cwd=tmp,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("no active plan directory exists", result.stderr)

    def test_stop_claim_check_warns_when_active_evidence_is_not_current_claim_relevant(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "IMPLEMENT.md").write_text(
                "\n".join(
                    [
                        "# IMPLEMENT.md",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk echo unrelated` printed `ok`.",
                    ]
                )
            )

            result = run_hook(
                STOP_CLAIM_CHECK,
                {
                    "hook_event_name": "Stop",
                    "response": "The package-ready proof is complete.",
                },
                cwd=root,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("do not contain claim-relevant evidence", result.stderr)

    def test_compact_plan_check_warns_on_missing_implementation_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "PLAN.md").write_text("# plan\n")

            result = subprocess.run(
                [sys.executable, str(COMPACT_PLAN_CHECK)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=root,
                check=False,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("missing IMPLEMENT.md", result.stderr)


if __name__ == "__main__":
    unittest.main()
