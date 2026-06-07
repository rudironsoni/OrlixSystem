# Orlix Codex Harness Implementation Log

## Task Reference

`PLAN.md`

---

## Log

### 2026-06-07

**What happened:**

Moved canonical context/spec docs, replaced `AGENTS.md`, added `docs/harness`, added plan templates, added Orlix skills, added Codex custom agents, added Codex rules/hooks, retired unrelated local skills, and updated stale README/skill-lock references.

**Decision:**

Use Codex-native surfaces: `AGENTS.md`, `.codex/agents`, `.agents/skills`, `.codex/rules`, `.codex/hooks`, and `docs/plans`.

**Deviation from plan:**

None.

**Evidence:**

- Strict stale-reference scan across `AGENTS.md`, README, docs, `.agents`, `.codex`, and harness metadata returned no matches before this log was added.
- `rtk python3 -c 'import pathlib,tomllib; [tomllib.loads(p.read_text()) for p in pathlib.Path(".codex/agents").glob("*.toml")]; tomllib.loads(pathlib.Path(".codex/config.toml").read_text()); print("toml ok")'` printed `toml ok`.
- `rtk python3 -m json.tool .codex/hooks.json` passed.
- `rtk git diff --check` passed.
- `rtk codex execpolicy check --pretty --rules .codex/rules/orlix.rules -- git push origin main` returned `decision = prompt`.
- `rtk codex execpolicy check --pretty --rules .codex/rules/orlix.rules -- rm -rf Build` returned `decision = forbidden`.
- `rtk codex execpolicy check --pretty --rules .codex/rules/orlix.rules -- rtk timeout 18000 make -f OrlixOS/Makefile test PROFILE=release` returned `decision = prompt`.
- Synthetic generated-tree hook input was blocked with `ORLIX-HARNESS-BLOCK`.

**Next:**

Run final scans after this implementation log is added, then commit and push if verification remains clean.

---

## Deviations Summary

| Deviation | Reason | Plan updated? |
|---|---|---|
| None | N/A | N/A |

## Open Questions

- [ ] None.

## Resolved Questions

| Question | Answer | Date |
|---|---|---|
| Where does mutable task state live? | `docs/plans/active` while active and `docs/plans/completed` after completion. | 2026-06-07 |
| What replaces the earlier agent knowledge directory idea? | `docs/harness`. | 2026-06-07 |
