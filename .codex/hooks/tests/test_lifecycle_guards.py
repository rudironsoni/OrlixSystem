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

    def test_stop_claim_check_warns_on_macos_runtime_wording(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "This is aligned with macOS runtime behavior."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("macOS runtime/user wording detected", result.stderr)

    def test_stop_claim_check_allows_negated_macos_runtime_wording(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "No macOS runtime target was added."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("macOS runtime/user wording detected", result.stderr)

    def test_stop_claim_check_warns_on_generic_container_support(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "The plan adds Docker support."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("Generic Docker/container support wording detected", result.stderr)

    def test_stop_claim_check_allows_negated_container_support(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Do not add Docker support."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("Generic Docker/container support wording detected", result.stderr)

    def test_stop_claim_check_warns_on_unscoped_named_mechanism(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Next we create OrlixImageBuilder."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("named tool, workflow root, or mechanism-specific proof target", result.stderr)

    def test_stop_claim_check_allows_negated_named_mechanism(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Do not create OrlixImageBuilder."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("named tool, workflow root, or mechanism-specific proof target", result.stderr)

    def test_stop_claim_check_warns_on_unauthorized_workflow_delegation(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Split this into subagents and a parallel workflow."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("without explicit user authorization", result.stderr)

    def test_stop_claim_check_allows_negated_workflow_delegation(self):
        result = run_hook(
            STOP_CLAIM_CHECK,
            {"hook_event_name": "Stop", "response": "Do not spawn subagents without explicit user authorization."},
        )

        self.assertEqual(result.returncode, 0)
        self.assertNotIn("without explicit user authorization", result.stderr)

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

    def test_compact_plan_check_warns_on_stale_status_contradiction(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "PLAN.md").write_text("# plan\n")
            (plan / "IMPLEMENT.md").write_text(
                "\n".join(
                    [
                        "# IMPLEMENT.md",
                        "",
                        "## earlier",
                        "",
                        "Current status:",
                        "",
                        "- pending on simulator proof.",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk echo first`",
                        "",
                        "## later",
                        "",
                        "Current status:",
                        "",
                        "- green after iOS Simulator proof succeeded.",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk xcodebuild test` succeeded.",
                    ]
                )
            )

            result = subprocess.run(
                [sys.executable, str(COMPACT_PLAN_CHECK)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=root,
                check=False,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("stale pending/blocked status", result.stderr)

    def test_compact_plan_check_warns_without_recent_handoff_or_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init"], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            plan = root / "docs" / "plans" / "active" / "demo"
            plan.mkdir(parents=True)
            (plan / "PLAN.md").write_text("# plan\n")
            (plan / "IMPLEMENT.md").write_text(
                "\n".join(
                    [
                        "# IMPLEMENT.md",
                        "",
                        "**Evidence:**",
                        "",
                        "- `rtk echo old` recorded old evidence.",
                    ]
                )
            )

            result = subprocess.run(
                [sys.executable, str(COMPACT_PLAN_CHECK)],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=root,
                check=False,
            )

        self.assertEqual(result.returncode, 0)
        self.assertIn("no recent current-status or handoff marker", result.stderr)


if __name__ == "__main__":
    unittest.main()
