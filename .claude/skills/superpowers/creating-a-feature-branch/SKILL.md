---
name: creating-a-feature-branch
description: Use when starting feature work that needs isolation from the main branch - creates a feature branch locally for development
---

# Creating a Feature Branch

## Overview

Create an isolated feature branch for development work. No worktrees, no sandbox fighting — just a clean branch.

**Core principle:** Branch first, implement second, merge when done.

**Announce at start:** "I'm using the creating-a-feature-branch skill to set up a feature branch."

## The Process

### Step 1: Detect Current State

```bash
git branch --show-current
git status
```

**If already on a feature branch:** Skip to Step 3.

**If on main/master:** Continue to Step 2.

### Step 2: Create Feature Branch

```bash
# Ensure main is up to date
git switch main
git pull

# Create and switch to feature branch
git switch -c feature/<short-description>
```

Branch naming convention: `feature/<short-description>` (kebab-case, descriptive)

### Step 3: Verify Clean Baseline

```bash
# Run the project's test suite. Detect which to use from the project:
#   Node (package.json) → npm test
#   Rust (Cargo.toml)   → cargo test
#   Python (pyproject.toml) → pytest
#   Go (go.mod)         → go test ./...
#   CMake (C++ projects) → cd build && ctest
```

**If tests fail:** Report failures, ask whether to proceed or investigate.

**If tests pass:** Report ready.

**If no test framework detected:** Report "No tests found — baseline verified by build passing."

### Report

```
Feature branch ready: feature/<short-description>
Tests passing (<N> tests, 0 failures)
Ready to implement <feature-name>
```

## Quick Reference

| Situation | Action |
|-----------|--------|
| Already on feature branch | Skip creation (Step 1) |
| On main/master | Create feature branch (Step 2) |
| Tests fail during baseline | Report failures + ask |
| No package.json/Cargo.toml | Skip dependency install |

## Common Mistakes

### Implementing on main

- **Problem:** Working on main, then realizing you need a branch
- **Fix:** Always create the branch first, before any code changes

### Skipping baseline tests

- **Problem:** Can't distinguish new bugs from pre-existing issues
- **Fix:** Run tests on the clean main branch before starting

### Bad branch names

- **Problem:** `feature/fix`, `feature/work`, `feature/test` — unhelpful
- **Fix:** Use descriptive names: `feature/structured-logging`, `feature/udp-heartbeat`

## Red Flags

**Never:**
- Implement on main/master without explicit user consent
- Skip baseline test verification
- Proceed with failing tests without asking

**Always:**
- Create branch before any code changes
- Run baseline tests on clean main
- Use descriptive kebab-case branch names
