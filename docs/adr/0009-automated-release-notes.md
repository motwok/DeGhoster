# 0009 — Automated release notes via Release Drafter

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus
- **Relates to:** [0008](0008-versioning-and-packaging.md) (GitVersion + packaging)

## Context

`master` is protected and changes land only through pull requests (even the
maintainer's own; external PRs are declined for security, see
[CONTRIBUTING.md](../../CONTRIBUTING.md)). We want a changelog that is compiled
automatically from those PRs, without hand-maintaining version numbers and without a
CI job pushing commits back to the protected branch.

Versioning is already owned by **GitVersion**, which derives the version from git and
release **tags** ([ADR-0008](0008-versioning-and-packaging.md)). Any changelog
automation must not fight that: the git tag stays the single source of truth for the
version.

## Decision

Use **Release Drafter** (`.github/release-drafter.yml` +
`.github/workflows/release-drafter.yml`). On every merge into `master` it rebuilds a
**draft** GitHub Release from the merged PRs, grouped by label (Features / Bug Fixes /
Documentation / Maintenance) and proposing the next semver from PR labels. Publishing
the draft creates the tag `vX.Y.Z`; GitVersion reads that tag for the build. A PR
autolabeler maps Conventional-Commit-style titles/branches to labels.

`CHANGELOG.md` in the repo root is a short entry point that explains the process and
links to the Releases page (the living changelog).

## Consequences

- **+** Zero manual changelog upkeep; notes come straight from PR titles.
- **+** No bot commits to the protected `master` — Release Drafter only writes a *draft*
  release, so branch protection is untouched.
- **+** Version stays owned by GitVersion/tags; Release Drafter only *proposes* the
  next tag at publish time.
- **−** Quality depends on good PR titles and correct labels (mitigated by the
  autolabeler).
- **−** The changelog lives on the Releases page rather than as full prose committed
  in `CHANGELOG.md`.

## Alternatives considered

- **Action that appends to `CHANGELOG.md` on merge and pushes back to `master`:**
  rejected — requires the CI token to bypass branch protection and a loop guard, and
  duplicates version bumping that GitVersion already does.
- **Release Please:** rejected — it would take ownership of versioning (Conventional
  Commits → version + tag), overlapping with GitVersion.
