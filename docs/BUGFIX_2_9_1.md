# Forge 2.9.1 — Stability and Optimization

[Forge wiki](../GUIDE.md) · [Release coverage](FORGE_2.md)

This update preserves project/schema/API versions and existing defaults. Native transport, physics, graphics, asset and Python library implementations remain replaceable outside the documented Core boundary. Core changes enforce existing validation contracts and compile Python sources through canonical project paths.

## Fixed behavior

| Area | Problem | Result |
| --- | --- | --- |
| ENet LAN | A bandwidth-period counter was treated as pending data, refusing drained sends and failing to bound actual queued packets. Empty payloads bypassed the byte check. | Packet ownership tracks pending payload bytes until release; `queue_events` also caps outgoing packets per peer. `pending_bytes` and `pending_messages` report the actual queue. |
| Network lifetime | Closed scene hosts left weak records indefinitely; application candidate records grew outside transactions. Erasing retired hosts individually shifted the remaining vector repeatedly. | Lifetime records are compacted in batches, candidate tracking is bounded and active only during preparation, and frame cleanup compacts once. Invalid scene ownership is rejected before a transport opens. |
| Bullet configuration | Switching from legacy physics checked child local coordinates and omitted gravity validation, committing a backend that failed on its first query/step. | World transforms and gravity pass Bullet limits before configuration changes. Rejection preserves the previous backend. |
| Fonts | Empty input reached pointer-only font parsing and crashed. Font generations ignored file size. | Collection/header/table-range checks reject empty/truncated inputs, unusable vertical metrics report errors, and cache keys include size. These structural checks do not certify arbitrary malformed fonts or replace a full font sanitizer. |
| Resource validation | Whole-project scans could read external JSON, Python or texture symlinks without applying the runtime's project boundary. Directories ending in `.json` were treated as files. | Selected resources resolve through the existing project boundary; regular file checks exclude directories. Aliases staying inside the project remain supported. |
| Replay and input | Replay used a predictable `.tmp` filename and could follow an existing symlink. Failed ActionMap construction retained its callback. Recording limits accepted invalid values. | Replay uses the save library's unique atomic write; input listeners register only after successful construction; recording capacity requires a positive integer. |
| LOD resources | Mesh removal ignored live procedural LOD references. Scene preloading omitted LOD assets and emitter textures. | Referenced LOD meshes cannot be removed; preloading discovers those resources and leaves virtual render targets/procedural meshes to the game. |
| Failure cleanup | Glyph/decoded-image buffers could leak during later allocation failures; partial asset-worker startup could terminate while destroying joinable threads. | Bitmap/image ownership is scoped and partial worker creation joins existing workers before propagating the error. |
| Windows sockets bootstrap | Static Protobuf selected `/MT`, whereas Forge and Valve sockets use `/MD`, producing an MSVC runtime mismatch. | The private bootstrap explicitly builds static Protobuf with the dynamic CRT. Rerun `tools/network_dependencies.py` before rebuilding sockets from an older private prefix. |

## Compatibility

Existing ENet callers should handle `send(...) == False` by servicing/polling and retrying later. The packet cap now also applies to zero-byte payloads; accepted packets retain capacity until transport release. ENet's `pending_bytes` diagnostic now means outstanding application payload bytes rather than bandwidth-period traffic. No wire format changed.

Projects relying on resources outside their root must move them inside and configure their paths; external symlinks were already unsupported by runtime asset access and packaging. Shader formats, graphics backend selection, Steam App ID/disable settings, editor independence, save envelopes and project API version 1 are unchanged.

## Regression checks

Run from a configured checkout:

```sh
cmake --build build --parallel 4
ctest --test-dir build -C Release --output-on-failure
python tests/graphics.py build/bin/forge
```

`network_native` covers acknowledged queue capacity, empty-packet limits, closed-host churn and candidate ownership using actual localhost traffic. `features` covers font errors, resource escapes, atomic replay writes, failed input construction and complete preloading. `simulation` covers rejected/successful Bullet switches; `rendering` covers procedural LOD resource lifetime. Graphics checks require a real device and are separate from CTest. The failed-font reload regression compares real frames before/after rejection, then repairs the font and verifies recovery.

The benchmark tool measures Python/native localhost work, intentional yielding and backpressure, rather than game FPS:

```sh
python tools/benchmark_network.py build/bin/forge --backend lan --messages 20000 --size 256
```

Target-platform checks are required for Windows and the optional official Steamworks SDK adapter. Open-source Valve sockets tests cannot establish Steam authentication, overlay, achievements, lobbies or relay behavior. Asset-worker creation exhaustion and allocator failure cleanup have been reviewed by source inspection; routine tests do not simulate every resource-exhaustion condition.

## Verified for this release

On Apple M2 / arm64 / macOS 27.0.1:

- Native release compilation succeeded; all **12 CTest suites passed** in 34.94 seconds.
- The networking-disabled native test target built and passed, preserving the optional transport path.
- Three relevant graphical checks passed on **OpenGL and Metal**, separately: extreme glyph dimensions, large/responsive UI at three window sizes, and failed-font reload frame preservation/recovery. Audio was silent during these checks; this does not verify playback hardware or every graphical suite.
- A fresh directory package passed all 775 manifest hashes/notices and five headless scenes from another working directory with invalid host `PYTHONHOME`/`PYTHONPATH`: welcome, simulation, authoring, optimization and networking.
- Changed Python sources parse successfully; changed public-document links and Git whitespace checks pass.

One matched LAN loopback sample sent 20,000 payloads of 256 bytes: 2.9.0 took **1.015 seconds**, while 2.9.1 took **0.101 seconds**. Send refusals fell from 6,834 to 141, with zero reported drops in both runs. The pending-packet fix removes false bandwidth-period backpressure; this single local workload is not a universal speedup, frame-rate result or timing requirement.

Windows/Direct3D and official Steamworks SDK/client behavior were not tested on this host. Windows CRT alignment is a source-confirmed build correction requiring target-platform verification.
