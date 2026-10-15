# Forge 2.12.2 — Documentation and Starter Cleanup

[Forge wiki](../GUIDE.md) · [Documentation index](README.md) · [Release archive](releases/README.md)

README now focuses on setup, running a game, and navigation. Current capabilities/limits and test requirements are separate from historical releases. Earlier maintenance notes moved from `docs/BUGFIX_*.md` to `docs/releases/`; the release index links their new locations. Technical reference filenames remain stable.

Outdated test-suite counts and release-session statements were removed from current manuals. Desktop CMake registers 16 base suites plus the optional shader compiler; the launcher still runs seven integration suites. Backend and headset checks remain separate, with their target-platform limitations documented.

The particle benchmark example supplies its required particle count. Configuration examples for optimization, localization, particles, and networking are valid JSON objects, and installation lists the optional OpenXR build flag. Dependency versions remain defined by the unchanged lock file.

The starter now copies the curated documentation index, references, wiki, and complete release archive, including the previously omitted 2.5.1 maintenance note. Ignored local reports have moved out of the source documentation tree. Built-in examples, assets, licenses, dependency pins, public APIs/formats, and runtime behavior remain unchanged. No Core implementation changes are included.

Validation checks cover documentation paths/anchors in the checkout and a clean Git export, starter copying and exclusion of local output, generated-project validation/headless execution, and relevant launcher/distribution regressions. Runtime graphics/device behavior was not changed by this release.

The macOS Release build and `ctest --test-dir build -C Release --output-on-failure -R '^integration$'` passed. A clean index export passed checks for 44 Markdown files, 371 local links/anchors, and 27 current JSON examples. A starter generated from that export included all public documentation, passed project validation, and ran headless for 30 frames. Windows, Android, GPU, audio-device, and headset checks were not rerun for this documentation/tooling release.
