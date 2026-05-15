# ADR 0003: Default To The App Store Profile

## Status

Accepted

## Context

Orlix has App Store, development, and enterprise profiles. The App Store profile carries the strictest product constraints.

Defaulting to a broader profile risks depending on behavior that the real product cannot use.

## Decision

Normal builds default to `PROFILE=appstore`. Development and enterprise profiles are explicit choices.

## Consequences

Milestone 1 proof requires App Store and development profile builds.

Build targets and documentation must not imply development behavior is the normal product contract.

The enterprise profile remains first-class, but it is not required proof in Milestone 1.
