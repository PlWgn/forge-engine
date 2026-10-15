# Pinned Build Dependencies

Only this README and [dependencies.lock.json](dependencies.lock.json) belong in source control. `tools/dependencies.py` fetches pinned sources and verifies their SHA-256 checksums over TLS. Downloaded libraries stay ignored; CMake does not fetch them. The lock is the version/checksum source of truth.

The build uses GLFW, GLM, pybind11, nlohmann JSON, stb, miniaudio, Assimp, and double-precision Bullet CPU libraries. The optional editor uses Dear ImGui; native Metal/Direct3D backends use glslang/SPIRV-Cross. Assimp importers are restricted to OBJ, glTF/GLB, FBX, and COLLADA. The default Noto Sans font is copied to `graphics` only when no font is present, with its SIL OFL license retained.

ENet supplies the default independent LAN transport. Optional Valve sockets use private Protobuf/OpenSSL dependencies; the official Steamworks SDK is supplied separately by the developer. Optional VR uses the pinned OpenXR sources. See [installation](../docs/wiki/installation.md), [networking](../docs/wiki/networking.md), and [VR](../docs/wiki/vr.md) for setup and flags.

Dependency notices and license files remain in source resources and standalone packages, including Assimp's compiled contrib components. [THIRD_PARTY.md](../THIRD_PARTY.md) records their origin and licensing; no dependency version is changed by documentation cleanup.
