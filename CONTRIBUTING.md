# Contributing

Useful bug reports, clearer instructions and focused fixes are welcome. Start with an issue for a larger feature so its behavior and scope can be discussed before implementation.

## Report a problem

Use the [bug report form](https://github.com/NeTriXoN-BoT/drifters-toolkit/issues/new?template=bug_report.yml). Include the toolkit version, operating system, game edition, exact steps, expected result and actual result. For Linux, include the distribution and whether you use X11 or Wayland. State whether the problem concerns PAKs, an ASI/DLL plugin, a profile, saves or Workshop.

Run Diagnostics when relevant. Review any report or screenshot before attaching it; remove personal paths, account identifiers, credentials and authorized download links. Link a mod’s original page instead of uploading its package. Do not attach game files, real saves or memory dumps to public issues.

Use [private vulnerability reporting](SECURITY.md) for security issues.

## Suggest an improvement

Use the [improvement form](https://github.com/NeTriXoN-BoT/drifters-toolkit/issues/new?template=improvement.yml). Explain the task you are trying to finish, where the current workflow gets in the way and what a useful result would look like. A concrete example helps more than a long feature list.

## Send a change

1. Fork the repository and create a branch for one change.
2. Follow [BUILDING.md](BUILDING.md) and reproduce the issue with disposable fixtures where possible.
3. Keep the change focused and preserve upstream notices when editing third-party material.
4. Run the relevant native or interface tests. Add a regression test when it protects meaningful behavior.
5. Explain the problem, the resulting behavior, your test commands and any platform you could not check in the pull request.

The native backend owns file operations and authorization. The interface must not bypass stopped-game checks, reviewed plans, path validation, recovery records or integrity verification. Treat uncertain files and unsupported formats as uncertain. Keep profile isolation limited to PAKs and preserve shared saves and unrelated files.

Keep generated binaries, dependency caches, downloaded mods, saves, credentials and local state out of commits. Public releases remain Local Edition until Nexus integration is approved and tested. Code changes need their matching generated assets when the build requires them; avoid unrelated generated-file churn.

Contribute code and assets you have permission to share. Original project contributions use the repository’s MIT licence; third-party components retain their own terms. Do not include proprietary game assets or redistribute someone else’s mod without permission.
