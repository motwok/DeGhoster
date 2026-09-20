# Contributing to DeGhoster

Thanks for your interest in DeGhoster! 🙂

This project follows a [Code of Conduct](CODE_OF_CONDUCT.md) — please be respectful.

> **Security issue?** Do **not** open a public issue — report it privately, see
> [SECURITY.md](SECURITY.md).

## Please use Issues — not Pull Requests

**Pull requests are not accepted — for security reasons.** DeGhoster injects a DLL
into other processes and configures autostart; it is exactly the kind of tool where a
subtle or malicious code change could do real harm. To keep the supply chain
trustworthy, every line that ships is written and reviewed by the maintainer alone.
Unsolicited PRs will therefore be closed unmerged — please don't spend effort on them.

Instead, contribute through **GitHub Issues**:

- 🐞 **Bug reports** — [open an issue](https://github.com/motwok/DeGhoster/issues)
- 💡 **Feature requests** — [open an issue](https://github.com/motwok/DeGhoster/issues)

Ideas, reproductions and well-described problems are genuinely valuable and the best
way to help.

## Writing a good bug report

Please include:

- **What happened** and **what you expected**.
- The **app that left the ghost window** (e.g. WhatsApp, Teams) and whether the
  cursor disappeared, clicks were swallowed, or both.
- Your **Windows version** (e.g. Windows 11 23H2) and whether it's 64-bit.
- How you installed DeGhoster (**MSI** or **portable ZIP**) and the **version**
  (see the file's Properties → Details, or the About dialog).
- **Steps to reproduce**, if you can — even rough ones help.

## Feature requests

Describe the problem you're trying to solve, not just a proposed solution — it helps
find the best fit. Note that not every request will be implemented; DeGhoster
intentionally stays small and focused.

## Forking

DeGhoster is licensed under the **GNU AGPL v3** (see [LICENSE](LICENSE)). You are of
course free to fork and modify it under those terms — just note that changes won't be
merged back here.

## Support the project

If DeGhoster helps you, you can support it: [Buy Me a Coffee](https://ko-fi.com/motwok) ☕
