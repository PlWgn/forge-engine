# Installation and Build Setup

[Forge wiki](../../GUIDE.md) · **Installation and Build Setup**

Install and compile the engine once. Games use that runtime from their own content directories.

- [Install the toolchain](#install-the-toolchain)
- [Optional build components](#optional-build-components)
- [Confirm the installation](#confirm-the-installation)

## Install the toolchain

### macOS

Install Command Line Tools (`xcode-select --install`), Python 3.10+ with development headers, and CMake 3.24+. Python from python.org is recommended for portable builds; the engine targets the architecture of the Python used to compile it. Apple's system Python is not recommended for this purpose.

```sh
python3 -m venv .tools
source .tools/bin/activate
python -m pip install cmake==3.31.6
python tools/dependencies.py
python tools/forge.py compile
```

`dependencies.py` downloads pinned libraries and a free font. Internet access is needed only to install dependencies; subsequent compilation works offline. The script verifies TLS certificates. If certificate validation fails, repair the installed Python's certificate setup rather than disabling verification.

A clean checkout contains no `.tools`, `build`, `dist`, or downloaded libraries: Git ignores them. `vendor` contains only its README and lock file. These commands create the local environment, download dependencies, and compile the engine. Built-in scenes and their source assets are tracked; local screenshots, validation reports, saves, and logs are excluded from the source distribution.

### Windows

Install Visual Studio 2022 Build Tools with Desktop development with C++, Windows SDK, CPython x64 from python.org, and CMake. Run the commands in Developer PowerShell or Native Tools Command Prompt:

```powershell
python -m venv .tools
.tools\Scripts\Activate.ps1
python -m pip install cmake==3.31.6
python tools/dependencies.py
python tools/forge.py compile
python tools/forge.py dev
```

All components must target the same architecture, such as x64. If venv activation is blocked, invoke `.tools\Scripts\python.exe` directly. MSVC receives `/utf-8` to support Unicode strings in source files.

CMake produces `build/bin/forge` on macOS and usually `build/bin/Release/forge.exe` on Windows. The Python launcher locates the appropriate binary automatically. The default graphics path needs OpenGL 3.3 Core; macOS uses a forward-compatible Core context. Optional Metal on macOS and Direct3D 11 on Windows have their own device requirements. [Select a backend](rendering.md#choose-a-graphics-backend) through engine.json.

## Optional build components

The launcher enables the builtin ImGui editor and the platform-native backend by default: Metal on macOS, Direct3D 11 on Windows. OpenGL remains available and the configured default. The native backends use pinned glslang/SPIRV-Cross sources to translate GLSL. Dependency download verifies SHA-256 against `vendor/dependencies.lock.json`; CMake itself does not download sources.

| Launcher option | Effect |
| --- | --- |
| `compile --without-editor` | Excludes ImGui; document/runtime editor APIs, run/dev/build remain available |
| `compile --without-metal` | Excludes macOS Metal/Objective-C++/framework dependencies |
| `compile --without-directx` | Produces an OpenGL-only Windows runtime |
| `compile --shader-tools` | Enables translator/compiler tests even without a native graphics backend |

These switches also apply to `configure` and dependency setup where relevant. Repeat the chosen flags when reconfiguring/compiling; an ordinary compile restores the default components. To build without the builtin shell:

```sh
python tools/dependencies.py --without-editor
python tools/forge.py compile --without-editor
```

Direct CMake builds use `FORGE_WITH_EDITOR`, `FORGE_WITH_METAL`, `FORGE_WITH_DIRECT3D11`, `FORGE_WITH_SHADER_TRANSLATOR`, and `FORGE_MODULE_SOURCES`. See [Metal](../METAL.md) and [Direct3D](../DIRECT3D11.md) for complete flags and shader contracts. CMake requires the dependencies to be present first. A generated content project shares the engine executable; compile a custom runtime with `compile --project path/to/game/engine.json` to include that game's `native_modules`.

## Confirm the installation

From the engine checkout with the environment activated:

```sh
python tools/forge.py validate --no-open-log
python tools/forge.py run --headless --frames 30 --no-open-log
python tools/forge.py dev
```

The first two commands check configuration/resources and logic. The third opens a real window. Run [tests](development.md#validation-and-test-suites) appropriate to the target device before shipping. Initial downloads need internet access; Forge has no mandatory network service at runtime. If compilation fails, inspect the compiler output and `forge.log`; using an incompatible Python architecture/header/library is a setup error, not a scene error.
