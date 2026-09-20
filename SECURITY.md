# Security Policy

DeGhoster injects a small hook DLL into other processes and configures a per-user
autostart entry. That makes it security-sensitive by nature, and reports are taken
seriously.

## Reporting a vulnerability

**Please report vulnerabilities privately — do not open a public issue, pull request,
or discussion for a security problem.**

Use **GitHub's private vulnerability reporting**:

1. Go to the repository's **Security** tab →
   **[Report a vulnerability](https://github.com/motwok/DeGhoster/security/advisories/new)**.
2. Describe the issue with enough detail to reproduce and assess it.

This keeps the report private between you and the maintainer and integrates with
GitHub Security Advisories for a coordinated fix and disclosure.

### What to include

- A clear description of the vulnerability and its **impact**.
- **Steps to reproduce** (or a proof of concept).
- Affected **version** (MSI/ZIP and the version string) and your **Windows version**.
- Any suggested mitigation, if you have one.

## What to expect

This is a single-maintainer project, so responses are best-effort rather than bound to
a fixed SLA. You can expect an acknowledgement of your report, an assessment, and — for
confirmed issues — a coordinated fix and disclosure. Credit is given to reporters who
want it.

Please give a reasonable amount of time for a fix before any public disclosure.

## Supported versions

Only the **latest release** receives security fixes. Older versions are not patched;
please update to the newest release.

| Version | Supported |
|---|---|
| Latest release | ✅ |
| Older releases | ❌ |

## Scope

DeGhoster deliberately does two things that can look alarming but are by design:

- it **injects a hook DLL** into ghost-window host processes to cloak the offending
  window in-process, and
- it **registers a per-user autostart** entry (`HKCU\...\Run`), never a machine-wide
  one.

These documented behaviors are **not** vulnerabilities on their own (see
[docs/Architecture.md](docs/Architecture.md) and the
[ADRs](docs/adr/README.md)). Genuine security issues include, for example: execution
of untrusted or attacker-controlled code, privilege escalation, tampering that lets a
third party abuse the injection or autostart mechanism, or a bypass of the ghost-window
safety checks that causes DeGhoster to act on legitimate windows.
