# Saves, Preferences, and Storage

[Forge wiki](../../GUIDE.md) · **Saves, Preferences, and Storage**

Store game-defined JSON in checked storage locations. SaveManager adds slot metadata, checksums, backup recovery, migrations, and autosave.

- [Storage locations and persistence transactions](#storage-locations-and-persistence-transactions)
- [Save slots and autosave](#save-slots-and-autosave)

## Storage locations and persistence transactions

`"storage": {"mode": "user", "application_id": "com.example.game"}` directs forge.save/SaveManager/preferences/localization to user directories. Compatibility default is project. macOS data/config/saves use `~/Library/Application Support/<id>`, cache uses Library/Caches, and logs use Library/Logs. Windows uses LOCALAPPDATA, falling back to APPDATA. user_path(kind,relative='') always returns a user path regardless of mode; kind=data/saves/config/cache/logs/captures. storage_path(relative) follows mode. Relative paths stay within the selected root; ../ and symlink escapes are rejected. The API returns a path; create parents for your own writes. FORGE_USER_ROOT overrides the root for tests. Use a stable, unique application_id.

During hot reload and editor apply/patch/undo/redo, forge.save and SaveManager.write freeze data/metadata; SaveManager.delete defers removal of primary/backup. Candidate rejection cancels the queue. New disk saves cannot be read before commit. Ordinary startup/scene loading outside these transactions retains immediate save/write/delete. forge.defer_persistence(callable,include_initialization=True) also defers during ordinary scene initialization by default and invokes immediately outside loading. False excludes ordinary initialization only, not reload/authoring transactions. Callbacks must provide their own atomic writes; copy mutable captured data. Commit callbacks run sequentially: failures are logged, completed writes are not undone, and later callbacks continue. Arbitrary Python filesystem/network operations remain the game's responsibility.

## Save slots and autosave

forge.save/load remain low-level APIs. Use the independent saves module for game slots:

```python
from saves import SaveManager, SaveError

saves = SaveManager(
    version=2,
    migrations={1: lambda old: dict(old, coins=0)},
    validate=lambda data: isinstance(data, dict) and 'health' in data,
)
saves.write('1', {'health': 100, 'coins': 8}, title='Chapter 2',
            description='Before entering the city', metadata={'scene': 'city.py'})
try:
    state = saves.read('1')
except SaveError as error:
    forge.log(error, 'WARN')

saves.autosave(capture_game, interval=60, slot='auto')
```

The default directory is `<save_directory>/slots`; directory selects another path within the configured storage root ([storage and transactions](persistence.md#storage-locations-and-persistence-transactions)). Slot identifiers contain 1..80 ASCII letters, digits, underscores, or hyphens. The `forge.slot/1` envelope version is independent of your game's data version. It records data, version, UTC time, title, description, metadata, and a SHA-256 checksum of the envelope excluding checksum. The default size limit is 16 MiB, configured by max_bytes, which must be a positive integer. The checksum detects corruption, not deliberate tampering.

SaveManager revalidates the storage boundary on every operation, including listing slots and committing deferred writes/deletes. A directory redirected outside storage is rejected with SaveError; after changing the configured storage location, create a new manager. Excessive JSON nesting is treated as corruption, allowing the same backup recovery and status reporting as other damaged data. These checks do not provide a sandbox against arbitrary Python I/O or concurrent external filesystem changes.

Writes use a temporary file, flush/fsync, and atomic replacement. Before overwriting, the last verified primary becomes .json.bak; a corrupt primary never overwrites a valid backup. read(..., recover=True) tries backup when the primary is absent/corrupt and logs a warning. Recovery returns backup data without rewriting the primary. recover=False disables it. read('missing', default=...) returns default only when both primary and backup are absent; it does not hide corruption.

migrations[n] converts data from n to n+1 until the current version is reached. Missing migrations or newer versions raise SaveVersion; corruption raises SaveCorrupt or SaveError, and schema violations raise SaveError. All derive from SaveError. A validator may return False or raise an exception. Migrations do not rewrite files automatically; save the validated result yourself. Validators and restore must check game fields before mutating game state.

saves.info(slot) returns metadata and status empty/ok/recoverable/corrupt: empty means neither file exists, ok means a valid primary, recoverable means a valid backup when the primary is absent/corrupt, and corrupt means files exist without a valid copy. A recoverable result takes metadata/version from backup, source is backup, and error describes the primary problem. The menu enables loading and displays a translated recovery hint. info() and slots() neither write nor run migrations/game validators; read() checks data/version compatibility. saves.slots() lists primary and backup slots without duplicates; delete(slot) removes both, deferred during reload/authoring transactions. info()['version'] exposes newer data versions. Autosave registers a frame callback; its timer normally pauses with gameplay (while_paused=True changes this). close() stops autosave. The game defines how saved data maps to entities; JSON saving does not automatically serialize Python instances or the whole World.
