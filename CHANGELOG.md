# Changelog

## 0.1.0 - 2026-11-17 (tested in game: TEST-RESULT-0.1.0.txt)
### Added
- Rules in loot/*.ini: [Class], List (a nested path), Load, Add / Remove with names, folders, field filters and chance.
- Console nl.reload, nl.apply, nl.dump.

### Fixed (2026-11-18)
- An item added to a list is kept loaded (root set): a start-up owner asset may not keep it alive.

### Changed (2026-11-18, refinement)
- Writes into game data are recorded and re-imported at each map load when their assets are no longer loaded (NorseReflect RecordWrite/RecheckWrites) - a safety net; the freeing it guards against was seen once and did not repeat in testing.
