# Packaging, Distribution, and Attribution

[Forge wiki](../../GUIDE.md) · **Packaging, Distribution, and Attribution**

Build packages the compiled runtime and project content; it does not compile a new engine or require an editor. Ship separately for each operating system and architecture.

- [Standalone builds](#standalone-builds)
- [Protected content builds](#protected-content-builds-212)
- [macOS applications and signing](#macos-applications-and-signing)
- [License and required attribution](#license-and-required-attribution)
- [Verify a package](#verify-a-package)

## Standalone builds

```sh
python tools/forge.py build --output dist/MyGame
```

All copied directories in paths and python_paths are checked before packaging. Output cannot be inside any of them or replace the source project/parent. Paths normalize relative to the root: a valid ../Project/modules alias becomes modules in both copy plan and game.json. Each resource/icon destination is independently checked against the temporary staging directory. Packaged paths/python_paths, project.icon, and save_directory use normalized names. Nested symlinks must stay within the project; cycles and aliases containing output are rejected.

A directory build contains Game / Game.exe, game.json, configured asset directories, extra python_paths, private runtime, licenses, manifest.json with SHA-256 file hashes, and START.txt. Players do not need C++ sources or CMake. Ordinary builds include game Python sources; use the optional protected content mode below to deter casual browsing/extraction.

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

## Protected content builds (2.12)

```sh
python tools/forge.py build --project ../MyGame/engine.json --protect-content --output dist/MyGame-1.0
# macOS application:
python tools/forge.py build --project ../MyGame/engine.json --protect-content --output dist/MyGame-1.0.app
```

This optional **macOS/Windows** mode encrypts configuration, scenes, Python scripts/modules, shaders, models, textures, audio and other files from configured `paths`/`python_paths`. Paths with spaces/Unicode and overlapping content roots are supported. The distribution contains `content.forge`, a small public `game.json` with project/storage/logging metadata, and the usual executable/private Python/licenses/manifest. Project title/version remain public, and a macOS app icon remains visible to the OS. Engine/runtime files and readable third-party notices remain outside the encrypted content. Developer source files are unchanged; ordinary `build`, run/dev/edit and init keep their defaults. The option is rejected on other commands, including `build-quest`.

The launcher uses an embedded loader and private Python, checks the complete container before extracting any content, then loads configuration/assets through existing file APIs. A fresh temporary directory is restricted to its owner on POSIX; Windows uses the current user's temporary directory and inherited ACLs. The runtime deletes it on normal exit and handled failures, after closing runtime resources. No stable decrypted cache is kept. A crash, forced termination or OS shutdown can leave temporary plaintext; game code and another process running as the same user can access it while the game runs. Unpacking adds startup time and requires disk space for both the decrypted compressed archive and extracted content. Python/asset APIs that inspect `project_path` see the temporary root; persistent writes must use the storage/user-path APIs. **Protected builds select `storage.mode=user`**, preserving the configured application ID (default `org.forge.game`); they do not migrate old project-local saves. Choose a unique application ID for each game. Arbitrary Python writes to the project tree are temporary.

Limits: 100,000 archive entries, 64 GiB of uncompressed content, and a 64 GiB compressed archive. Encryption/decryption streams bounded 1 MiB records rather than buffering the game in memory. Archive paths are checked against absolute/parent/Windows-drive paths, reserved names, links/special files, Unicode/case aliases and file/directory conflicts before extraction. Project-native libraries (`.pyd`, `.dll`, `.dylib`, `.so`) are rejected in this mode because loaded libraries can prevent temporary cleanup; use an ordinary build for such projects. Native dependencies of private Python and compiled engine modules keep their usual packaging path. Protected packages support `run` and `validate`; editing/rebuilding needs the source project.

Container format `forge.content/1` is separate from the project schema and API versions. It holds a ZIP stream encrypted using the OS AES-256-CBC implementation (CommonCrypto/BCrypt), fresh IVs, independent random encryption/authentication keys and HMAC-SHA256 for each numbered record. Authentication covers the container identity, sequence, length, IV and ciphertext; an authenticated terminal record detects truncation. Authentication is checked before AES decryption, and the entire stream is checked before ZIP extraction. `manifest.json` hashes the container and directory executable as well as ordinary files; macOS bundle executables remain signature-managed. Manifest hashes and shipped-key authentication detect accidental damage and casual edits; they are not signatures proving publisher identity. Sign the final application/distribution through the platform's normal publishing process.

**This is casual extraction protection, not DRM or a guarantee of unreadable files.** The runnable game must ship its key, here embedded in its executable, and must recover plaintext. Someone with the package can recover that key, unpack the files or copy the whole game. Do not place secrets, private signing keys or server credentials in game content. Encryption creates no additional legal rights or licensing enforcement. Game ownership checks can use the existing optional [Steam services](networking.md#enable-steamworks) or a game-controlled licensing service; games choose the policy and any offline behavior. This option introduces no activation/network service and changes no Forge attribution or third-party license requirements.

`python tools/verify_package.py dist/MyGame-1.0` verifies hashes/notices and launches protected built-in examples from another cwd under private Python. Its `--graphics-backend` probe edits packaged scripts/settings and therefore requires an ordinary build; configure the source project and test actual protected-game frames separately for graphics verification.

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

## Optional VR (2.11)

See [VR and OpenXR](vr.md) for SteamVR/Quest Link, standalone Quest 3/3S APK builds, tracked input, rig transforms and platform verification limits. Existing flat-screen defaults and APIs are unchanged.
