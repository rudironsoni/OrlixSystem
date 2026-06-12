---
name: orlix-codex-handoff
description: Use for long Orlix sessions, compaction risk, archival, handoff to another agent, or any request to continue work from conversation history.
---

# Orlix Codex Handoff

Create a handoff that lets a fresh agent resume without rereading the whole chat.

## Destination

- For durable Orlix work, write handoffs under `docs/codex-handoffs/YYYY-MM-DD-topic.md`.
- For explicitly ephemeral handoffs, write to the operating system temporary directory.
- Do not archive, delete, prune, or mutate Codex local state.

## Required Content

- Repo path, branch, and latest relevant commit.
- Current goal and whether the goal is active, complete, blocked, or unknown.
- Active plan path and implementation log path.
- Completed work, files touched, and commands/tests run.
- Proof status, including what is green, partial, skipped, stale, or unverified.
- Known failures, constraints, and user corrections that must not be lost.
- Open decisions and the next 3-7 concrete steps.
- Suggested skills for the next agent.
- Reactivation prompt that names the exact plan/log/commit to read first.

## Rules

- Reference existing plans, ADRs, commits, diffs, and logs instead of duplicating them.
- Redact secrets, credentials, private tokens, personal identifiers, and bulky logs.
- Keep macOS described as build, simulator-control, fixture, oracle, or result-inspection host unless a verified runtime target says otherwise.
- Do not claim completion from handoff quality. Handoff is continuity evidence only.
