# Dependencies and redistribution

Original Forge code and starter assets are licensed under the [Forge Attribution License 1.0](LICENSE). [CORE.md](CORE.md) defines the core boundary, and [ATTRIBUTION.md](ATTRIBUTION.md) describes the required in-game notices. This does not replace or change any third-party license below.

The engine uses these pinned dependencies. Run `python tools/dependencies.py` to reproduce them; URLs and SHA-256 hashes are saved in `vendor/dependencies.lock.json`. Archive/header content is verified against that lock on subsequent downloads. Dependencies are available locally after bootstrap, so CMake itself does not download anything.

| Dependency | Version | License / source |
| --- | --- | --- |
| GLFW | 3.4 | zlib/libpng, https://github.com/glfw/glfw/blob/3.4/LICENSE.md |
| GLM | 1.0.1 | MIT, https://github.com/g-truc/glm/blob/1.0.1/copying.txt |
| pybind11 | 3.0.1 | BSD-3-Clause, https://github.com/pybind/pybind11/blob/v3.0.1/LICENSE |
| nlohmann JSON | 3.11.3 | MIT, embedded in vendor/json.hpp |
| stb_image / stb_truetype | f0569113c93ad095470c54bf34a17b36646bbbb5 | MIT/public domain, license embedded in headers |
| miniaudio | 0.11.23 | MIT/public domain, license embedded in header |
| Noto Sans | Checksum-pinned Google Fonts snapshot | SIL Open Font License 1.1, graphics/FONT-LICENSE.txt |
| CPython | The version used to compile the engine | Python Software Foundation license, copied to runtime/LICENSE.txt |

Game builds include dependency notices under `licenses`, the font license alongside the font, and the CPython license. Additional Python packages and custom modules are the game developer's responsibility. OpenSSL or other libraries from the build Python may be copied with its extension modules; their accompanying notices are copied when available and are listed in the build manifest.

The generated textures, OBJ and notification WAV are original procedural starter assets and can be changed or used in your game.
