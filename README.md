# Forge 2.12.2

Forge 2.12.2 is a modular C++17 engine with Python scenes and scripts for macOS and Windows, with an optional Android/OpenXR host for Quest. This maintenance release organizes the documentation and includes the complete public manual and release archive in generated projects. [Release notes](docs/BUGFIX_2_12_2.md).

Use configuration and Python for ordinary game development; C++ recompilation is needed only for engine or native-module changes. The runtime supports 2D/3D scenes, UI/dialogue, animation, physics, particles, audio, localization, networking, and optional editing tools. OpenGL is the default; Metal on macOS and Direct3D 11 on Windows are optional. [Capabilities and limits](docs/FORGE_2.md).

## Build and run

This repository contains source. Run the following commands from the engine checkout. Initial setup downloads dependencies pinned by SHA-256; `.tools`, `build`, `dist`, and downloaded libraries stay outside Git.

On macOS, install Command Line Tools and Python 3.10+ with development headers:

```sh
python3 -m venv .tools
source .tools/bin/activate
python -m pip install cmake==3.31.6
python tools/dependencies.py
python tools/forge.py compile
python tools/forge.py validate --no-open-log
python tools/forge.py dev
```

On Windows, install CPython x64 and Visual Studio 2022 Build Tools with Desktop development with C++ and the Windows SDK. Use Developer PowerShell:

```powershell
python -m venv .tools
.tools\Scripts\Activate.ps1
python -m pip install cmake==3.31.6
python tools/dependencies.py
python tools/forge.py compile
python tools/forge.py validate --no-open-log
python tools/forge.py dev
```

See [installation](docs/wiki/installation.md) for requirements and optional editor, graphics, networking, and VR components. Python, JSON, and asset edits need no C++ recompilation.

## Create and distribute a game

With the environment activated, run these commands from the **engine checkout**:

```sh
python tools/forge.py init --output ../MyGame
python tools/forge.py dev --project ../MyGame/engine.json
python tools/forge.py build --project ../MyGame/engine.json --output dist/MyGame-1.0
```

`init` requires an empty target and copies content, examples, schemas, licenses, and documentation. The generated game shares the compiled engine. `build` validates the entire project and creates a standalone distribution with private Python; choose a new output for every build. On macOS, an output ending in `.app` creates an application bundle. [Packaging, optional protected content, signing, and limits](docs/wiki/packaging.md).

Try examples with `python tools/forge.py dev --scene interface.py`, `--scene world3d.py`, `--scene simulation.json`, or `--scene authoring.py`. Use `python tools/forge.py edit --scene editor-empty.json` for the optional builtin editor. [All examples and commands](GUIDE.md).

## Documentation and checks

| Purpose | Entry point |
| --- | --- |
| Learn the engine and find an API topic | [Forge wiki](GUIDE.md) |
| Browse manuals, technical references, and history | [Documentation index](docs/README.md) |
| Review current capabilities and implementation limits | [Coverage map](docs/FORGE_2.md) |
| Run regressions or measure performance | [Development and testing](docs/wiki/development.md#validation-and-test-suites) |
| Review past releases and migrations | [Release archive](docs/releases/README.md) |

`ctest --test-dir build -C Release --output-on-failure` runs the full configured desktop test set. The launcher `python tools/forge.py test` runs seven integration suites; graphics/device tests are separate. Test requirements, backend commands, and verification limits are maintained in the testing guide above.

The source distribution includes C++, Python modules, tools, examples/assets, schemas, tests, CI, documentation, and dependency/license records. Builds, environments, downloads, caches, player state, logs, captures, and local reports are ignored through [.gitignore](.gitignore). Bundled models, textures, font, audio, and examples are required source content.

## License and modification

Forge uses the custom **Forge Attribution License 1.0**, which is source-available and not OSI-approved. Commercial and closed-source games are allowed. Public products require attribution on startup/loading and menu/About screens. Unchanged official Core uses “Uses the Forge engine”; your Core modifications outside official releases/documented approval use “Built on the Forge engine (modified core)”.

The exhaustive [Core boundary](CORE.md) identifies the affected components. Other modules, shaders, physics, graphics, audio, and content remain adaptable under their licenses. Core changes are permitted; configuration/extensions are recommended for ordinary game development to ease upgrades. Full terms and notices: [LICENSE](LICENSE), [ATTRIBUTION.md](ATTRIBUTION.md), [NOTICE](NOTICE), and [THIRD_PARTY.md](THIRD_PARTY.md).
