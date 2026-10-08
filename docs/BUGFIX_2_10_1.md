# Forge 2.10.1 — Memory, Lifecycle and Recovery Fixes

[Forge wiki](../GUIDE.md) · [Release coverage](FORGE_2.md)

This update preserves project/schema/API versions and existing defaults. Core-origin changes are limited to `runtime.cpp` (save temporary files) and `reload.cpp` (pending watcher changes); see [CORE.md](../CORE.md). All other fixes stay within their adaptable components.

## Fixed behavior

| Area | Problem | Result |
| --- | --- | --- |
| Audio | Playing an undecodable file reached a miniaudio 0.11.23 failure path that reads a freed resource-manager node (heap use-after-free). | Files are probed with a decoder before `ma_sound_init_from_file`; successful probes are cached by size/modification time, so repeated plays skip the probe. |
| ENet LAN | Disconnecting a peer that was still connecting reset it without an ENet event, leaving a stale mapping; a later connection reusing that slot inherited the retired peer number. | The number is released at once and the next poll reports `disconnected` with the given reason. Peer numbers are never reused within a host. |
| Hot reload | A rejected candidate restored `sys.modules`, but changed-file tracking was replaced on the next poll, so `python_paths` packages edited before the failure stayed stale after the fix. | Watcher changes accumulate until a reload commits; overflow still invalidates managed roots. |
| Saves and documents | A regular `.tmp`/`.forge-tmp` file left by an interrupted write blocked that save or document permanently. | A leftover regular temporary file is replaced; symlinked or special temporary paths are still refused. |
| Asset cache | Failed generations hold no bytes, so LRU eviction never removed them; repeatedly editing a broken file accumulated entries. | Unreferenced failed generations of the same file are dropped when a new generation is cached. |
| Native input | `reset()` could leave bind-only contexts below the top of the context stack; `pop_context()` then exposed a missing context and `update()` raised `out_of_range`. | Stale lower stack entries are removed; a missing active context still falls back to `default`. |
| Model import | Infinite bone weights passed importer positivity filters and normalized to NaN; node and bone offset matrices were not checked. | Non-finite/negative bone weights and non-finite transforms are rejected during import. |
| Save slots | Any unrelated `*.json` file in the slot directory made `SaveManager.slots()` raise. | Files whose names are not valid slot ids are skipped. |

## Compatibility

LAN callers that disconnect a pending connection now receive a local `disconnected` event for that peer. Models with non-finite skinning data that previously loaded (and rendered NaN vertices) are now rejected with an explicit error. miniaudio itself is unchanged; upgrading the pinned dependency remains a separate, deliberate step.

## Regression checks

Each fix has a regression test that fails on 2.10.0: `input_native`, `network_native`, `integration`, `features` and `project_api`. The full suite also ran under AddressSanitizer/UndefinedBehaviorSanitizer on macOS, including the OpenGL and Metal GPU suites. Windows and Direct3D 11 were not run for this release.

```sh
python tools/forge.py compile
ctest --test-dir build -C Release --output-on-failure
python tools/forge.py test
```
