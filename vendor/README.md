Native libraries are fetched by `tools/dependencies.py` at pinned versions. The lock stores download checksums. Keep `dependencies.lock.json` in source control; generated library directories may be excluded.

The default free font is copied to `graphics` only if no font is already there. Its SIL OFL license is retained beside it.
