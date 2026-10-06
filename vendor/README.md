Native libraries are fetched by `tools/dependencies.py` at pinned versions. The lock stores download checksums. Keep `dependencies.lock.json` in source control; generated library directories may be excluded.

The default free font is copied to `graphics` only if no font is already there. Its SIL OFL license is retained beside it.

Forge 2.0 also pins Assimp 6.0.5 and Dear ImGui 1.91.9b. Assimp importers are restricted to OBJ, glTF/GLB, FBX and COLLADA. License notices from its contrib directory and Dear ImGui are included in packaged games.

Forge 2.1 pins Bullet Physics 3.25 (zlib license) and compiles only its CPU LinearMath, BulletCollision and BulletDynamics libraries with double precision. The downloaded source remains ignored; its notice is included in standalone builds as `licenses/bullet.txt`.

Forge 2.9 pins ENet 1.3.18 for independent default LAN, optional Valve GameNetworkingSockets 1.4.1, and optional private Protobuf 3.21.12/OpenSSL 3.5.9. Bootstrap/build instructions are in docs/wiki/networking.md. Downloads remain ignored; networking license snapshots are tracked under engine/resources/network-licenses. The official Steamworks SDK is supplied separately by the developer and is not part of this vendor bootstrap.
