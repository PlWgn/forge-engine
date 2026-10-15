# Forge editor API 1

Standard-library Python SDK for editor shells. Install from this folder or add it to the Python search path; provide a compiled Forge binary (builtin GUI optional). `Client`/`Document` speak local JSON-lines; `RuntimeClient` adapts the same commands inside native `edit`. No GUI toolkit, network service, or game dependency is required.

For the ready-to-use graphical workspace, compile the engine normally and run from the engine checkout:

```sh
python tools/forge.py edit --scene editor-empty.json
# Select another project's content:
python tools/forge.py edit --project ../MyGame/engine.json --scene editor-empty.json
```

Forge 2.13 includes a resizable Hierarchy, rendered Scene view, grouped Inspector, searchable Project assets, log Console and Extensions panel. Authoring uses the same transactional runtime commands available to your own shell; the interface needs no separate Python GUI dependency. [Workspace controls and limits](../docs/wiki/editor.md). `--shell none`, custom shells and `compile --without-editor` remain supported. The SDK is still editor-neutral and API 1; installing the SDK alone does not install the native GUI or a compiled runtime.

The [project API reference](../docs/PROJECT_API.md) defines the versioned protocol, format, extensions, conflict handling, and examples. SDK API version 1 is independent of the engine version.

Source and wheel preserve Forge Attribution License 1.0 and NOTICE. SDK modules are outside the engine Core list and can be adapted under their license.
