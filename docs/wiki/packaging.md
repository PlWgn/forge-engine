# Packaging, Distribution, and Attribution

[Forge wiki](../../GUIDE.md) · **Packaging, Distribution, and Attribution**

Build packages the compiled runtime and project content; it does not compile a new engine or require an editor. Ship separately for each operating system and architecture.

- [Standalone builds](#standalone-builds)
- [macOS applications and signing](#macos-applications-and-signing)
- [License and required attribution](#license-and-required-attribution)
- [Verify a package](#verify-a-package)

## Standalone builds

```sh
python tools/forge.py build --output dist/MyGame
```

All copied directories in paths and python_paths are checked before packaging. Output cannot be inside any of them or replace the source project/parent. Paths normalize relative to the root: a valid ../Project/modules alias becomes modules in both copy plan and game.json. Each resource/icon destination is independently checked against the temporary staging directory. Packaged paths/python_paths, project.icon, and save_directory use normalized names. Nested symlinks must stay within the project; cycles and aliases containing output are rejected.

A directory build contains Game / Game.exe, game.json, configured asset directories, extra python_paths, private runtime, licenses, manifest.json with SHA-256 file hashes, and START.txt. Players do not need C++ sources or CMake. Game Python sources are included; packaging is not intellectual-property protection.

On macOS:

```sh
cd dist/MyGame
./Game
```

On Windows, run Game.exe; it opens the game window without an explicit run command. The executable locates game.json beside it regardless of the working directory. Move all build files together. Players need no installed Python; private interpreter startup ignores PYTHONHOME/PYTHONPATH.

Packaging first writes to a temporary sibling directory and moves it to the destination only after success. Existing output is never overwritten. Choose a new versioned name or explicitly remove the old build. Output cannot be the project root, an ancestor, or a directory inside copied assets.

On macOS, Python and native extensions are copied, Mach-O dependencies are relocated to relative paths, and modified binaries receive ad-hoc signatures for local execution. Public distribution requires your Developer ID signature/notarization as appropriate. Output ending in .app creates an application bundle; a directory build runs its executable from the terminal. See [macos applications and signing](packaging.md) for signing/notarization; DMG creation is separate.

On Windows, packaging copies the Python DLL, standard library, DLLs, and runtime DLLs present alongside Python. If the selected Python distribution lacks MSVC runtime dependencies, install Microsoft Visual C++ Redistributable on the player's machine or provide an authorized app-local distribution.

Ordinary build cannot produce a Windows executable on macOS or vice versa. Compile and package separately on each target OS/architecture. .github/workflows/build.yml supplies macOS/Windows build tests and artifacts, plus Linux GPU checks. A workflow file's presence does not establish local Windows validation.

The package includes the selected CPython standard library, potentially tens of megabytes. storage.mode='project' requires the game directory to be writable for logs/saves. For Program Files or another protected directory, use storage.mode='user' to keep player data separately. .app packaging selects user storage automatically. See [storage and transactions](persistence.md#storage-locations-and-persistence-transactions).

## macOS applications and signing

```sh
python tools/forge.py build --output dist/MyGame.app
open dist/MyGame.app
python tools/forge.py sign --output dist/MyGame.app --identity 'Developer ID Application: Your Name (TEAMID)'
python tools/forge.py notarize --output dist/MyGame.app --keychain-profile your-stored-profile
```

.app includes Contents/MacOS/Game, Info.plist/PkgInfo, Resources/game.json, runtime, content, and ICNS converted from project.icon. Conversion uses sips/iconutil and needs macOS system services. The wrapper does not depend on a shell or cwd. Apps automatically use storage.mode=user. Directory packaging remains supported on macOS/Windows; Windows Game.exe uses app-local CPython DLLs.

Local packaging applies ad-hoc signatures for integrity, not Developer ID/Gatekeeper notarization. sign signs nested Mach-O binaries inside-out, enables hardened runtime/Python-library entitlements, updates the manifest, and seals the bundle. notarize creates a ZIP with ditto, invokes notarytool --wait, and staples it. Store credentials in Apple's Keychain beforehand, not engine.json. A real certificate/Apple Developer account is required; the commands do not create them. Local ad-hoc packaging does not establish Developer ID signing or notarization; verify those with your own credentials.

Directory manifest.json holds file SHA-256 hashes. In .app it resides in Resources, with paths relative to the bundle root. Game and CodeResources appear in signature_managed_files because external signing changes/verifies them. Also run codesign --verify --deep --strict MyGame.app. Do not remove CPython files or licenses from the bundle.

## License and required attribution

The project uses the custom [Forge Attribution License 1.0](../../LICENSE). Commercial use and closed-source games are allowed. The license is not OSI-approved.

Distributed, published, or publicly demonstrated products must show **“Uses the Forge engine”** on the startup/loading screen and in the main menu or an About section accessible directly from it. With your own core changes outside an official release or documented approval by the affected copyright holders, show **“Built on the Forge engine (modified core)”** in both places. Internal tests are exempt from these screens.

The exhaustive core list is defined in LICENSE and explained in [CORE.md](../../CORE.md). Everything outside the core may be changed/extended for a game unless separately licensed: scenes, scripts, modules, settings, shaders, physics (engine/src/world.cpp), graphics, and audio. Changing only these does not require modified-core attribution. Dependencies/fonts retain their licenses.

Core changes are also permitted with appropriate origin attribution. Renaming/moving core code does not remove the requirement. Ordinary compilation of unchanged source for another OS/architecture does not count as a core modification.

Keep LICENSE and NOTICE in the distribution; build and init copy them automatically. Game developers place the required UI notices themselves; the engine does not automatically check menus/loading screens. Examples and translated notices: [ATTRIBUTION.md](../../ATTRIBUTION.md).

## Verify a package

Run from the engine checkout:

```sh
python tools/verify_package.py dist/MyGame-1.0
```

The verifier checks manifest hashes/notices and requires and launches welcome, simulation, and authoring; it also launches optimization when present under private Python from another working directory with hostile host Python environment variables. It uses a temporary package copy so test logs/preferences do not modify the distribution. For a customized game that removes those examples, use its own smoke checks instead of this source-distribution verifier. For actual pixels, use the verifier's graphics options described by `python tools/verify_package.py --help`, with a supported device; a headless pass does not verify graphics/audio hardware.

Do not delete private Python files or dependency/shader/font notices to reduce size. Signing changes must be reflected in the manifest by the signing tool. [Backend documentation](rendering.md#choose-a-graphics-backend) describes platform-specific compiler notices and standalone graphics checks.
