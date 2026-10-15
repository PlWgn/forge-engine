# Forge Documentation

Start with the [Forge wiki](../GUIDE.md) for setup, a first scene, examples, and task-based navigation. These documents describe current behavior unless explicitly marked as historical release records.

## Current manuals

| Location | Purpose |
| --- | --- |
| [Wiki entry point](../GUIDE.md) | Getting started, commands, examples, and links to every topic |
| [Topic guides](wiki/installation.md) | Installation, projects, scenes, APIs, rendering, physics, audio, input, VR, and other game-development tasks; navigate through the wiki |
| [Capabilities and coverage](FORGE_2.md) | Current implementation, test entry points, and limits |
| [Development and testing](wiki/development.md) | Reload/diagnostics, full suite list, GPU requirements, and benchmarks |

## Detailed references

| Guide | Contract |
| --- | --- |
| [Project API](PROJECT_API.md) | Open documents, shells, SDK, extensions, conflicts, and runtime editing |
| [Direct3D 11](DIRECT3D11.md) | Windows backend, portable GLSL, and native HLSL |
| [Metal](METAL.md) | macOS backend, native MSL, resources, and synchronization |
| [Rendering optimization](RENDER_OPTIMIZATION.md) | LOD/culling settings, conservative bounds/proxies, and native policy replacement |

## Releases and licensing

[2.13.0 release notes](FORGE_2.md#forge-2130--default-editor-workspace) describe the default editor workspace. [Release history and archived fixes](releases/README.md) preserve past changes and release-specific validation; use the current manuals for commands and supported behavior. Earlier `docs/BUGFIX_*.md` notes have moved under `docs/releases/`.

The binding [license](../LICENSE), [Core boundary and origin](../CORE.md), [attribution](../ATTRIBUTION.md), and [third-party notices](../THIRD_PARTY.md) remain at the project root. Generated games include this documentation, schemas, examples, and licenses. Local reports, screenshots, builds, and dependency downloads belong in ignored output directories and are not documentation dependencies.
