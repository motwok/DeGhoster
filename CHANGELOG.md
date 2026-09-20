# Changelog

The full, per-version changelog lives on the
**[Releases page](https://github.com/motwok/DeGhoster/releases)**.

Release notes are compiled **automatically** from merged pull requests by
[Release Drafter](.github/release-drafter.yml): every merge into `master` rebuilds a
*draft* release, with entries grouped into **Features**, **Bug Fixes**,
**Documentation** and **Maintenance** (by PR label). When the draft is published it
creates the version tag `vX.Y.Z`, which [GitVersion](GitVersion.yml) then uses to
stamp the binaries and name the artifacts — see [Build.md](docs/Build.md) and
[ADR-0009](docs/adr/0009-automated-release-notes.md).

Versioning follows [Semantic Versioning](https://semver.org). The next version is
derived from PR labels: `breaking`/`major` → **major**, `feature`/`enhancement` →
**minor**, otherwise **patch**. Add the `skip-changelog` label to omit a PR.

## Unreleased

The upcoming changes are collected in the current **draft** on the
[Releases page](https://github.com/motwok/DeGhoster/releases). This file is an entry
point and process description; it is intentionally not edited by hand per change.
