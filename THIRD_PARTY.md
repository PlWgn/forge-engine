# Dependencies and redistribution

Original Forge code and starter assets are licensed under the [Forge Attribution License 1.0](LICENSE). [CORE.md](CORE.md) defines the core boundary, and [ATTRIBUTION.md](ATTRIBUTION.md) describes the required in-game notices. This does not replace or change any third-party license below.

The engine uses these pinned dependencies. Run `python tools/dependencies.py` to reproduce them; URLs and SHA-256 hashes are saved in `vendor/dependencies.lock.json`. Archive/header content is verified against that lock on subsequent downloads. Dependencies are available locally after bootstrap, so CMake itself does not download anything.

| Dependency | Version | License / source |
| --- | --- | --- |
| GLFW | 3.4 | zlib/libpng, https://github.com/glfw/glfw/blob/3.4/LICENSE.md |
| GLM | 1.0.1 | MIT, https://github.com/g-truc/glm/blob/1.0.1/copying.txt |
| pybind11 | 3.0.1 | BSD-3-Clause, https://github.com/pybind/pybind11/blob/v3.0.1/LICENSE |
| Assimp | 6.0.5 | BSD-3-Clause, https://github.com/assimp/assimp/blob/v6.0.5/LICENSE; internal dependency notices in licenses/assimp-contrib |
| Bullet Physics | 3.25 | zlib, https://github.com/bulletphysics/bullet3/blob/3.25/LICENSE.txt |
| Dear ImGui | 1.91.9b | MIT, https://github.com/ocornut/imgui/blob/v1.91.9b/LICENSE.txt |
| nlohmann JSON | 3.11.3 | MIT, embedded in vendor/json.hpp |
| stb_image / stb_truetype | f0569113c93ad095470c54bf34a17b36646bbbb5 | MIT/public domain, license embedded in headers |
| miniaudio | 0.11.23 | MIT/public domain, license embedded in header |
| Noto Sans | Checksum-pinned Google Fonts snapshot | SIL Open Font License 1.1, graphics/FONT-LICENSE.txt |
| Unicode CLDR cardinal rules | 48.0.0 | Unicode License v3, engine/resources/Unicode-LICENSE.txt |
| CPython | The version used to compile the engine | Python Software Foundation license, copied to runtime/LICENSE.txt |

Game builds include dependency notices under `licenses`, the font license alongside the font, and the CPython license. Additional Python packages and custom modules are the game developer's responsibility. OpenSSL or other libraries from the build Python may be copied with its extension modules; their accompanying notices are copied when available and are listed in the build manifest.

The generated textures (including the soft particle sprite and the PBR base/normal/ORM patterns), OBJ (including sphere/capsule meshes), animated triangle glTF and notification WAV are original procedural starter assets and can be changed or used in your game. The FBX regression test temporarily copies Assimp's own box fixture from vendor; it is not shipped as a Forge game asset. Assimp is compiled with OBJ, glTF/GLB, FBX and COLLADA importers, without exporters/tools/tests. macOS uses the system zlib; Windows uses Assimp's bundled zlib. Dear ImGui is used by the optional scene editor. Bullet is statically compiled with double precision from its LinearMath, BulletCollision and BulletDynamics CPU libraries; no demos, servers or PyBullet are included. Its unmodified license is shipped as `licenses/bullet.txt`.

The compiled localization data in `engine/include/forge/localization_data.hpp` derives from [CLDR JSON 48.0.0 plurals.json](https://github.com/unicode-org/cldr-json/blob/48.0.0/cldr-json/cldr-core/supplemental/plurals.json). Sample annotations were removed and locale keys normalized; the original SHA-256 is recorded in that header. The evaluator supports cardinal rules for ordinary JSON numeric counts; compact-exponent operands are zero. Engine UI translations are original Forge strings. The Unicode copyright/permission notice is preserved in `engine/resources/Unicode-LICENSE.txt` and copied to game builds and starter projects as `licenses/Unicode.txt`.

The PBR shader is an original Forge implementation of the metallic/roughness workflow (GGX distribution, Smith visibility approximation and Schlick Fresnel); it is not copied from a third-party renderer. Channel conventions follow the [Khronos glTF 2.0 material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials). Supporting glTF material maps does not imply complete glTF extension or renderer conformance. No new dependency is introduced in Forge 2.2.
