# Catalog provenance and compatibility evidence

`catalog.json` contains 74 entries for all 46 main files listed on the [Drifter's Toolkit Nexus files page](https://www.nexusmods.com/daysgone/mods/994?tab=files), observed on 23 October 2026. The `nexusFileId` values come from each file's actual manual-download link. No download credentials are included.

A download with several PAKs (tiers, optional extras, standalone pieces) has one entry per PAK: the source names the PAK with `member`, every entry carries the file's `nexusFileId` and `archiveSha256`, and `download` names the ZIP. Lasting Survival Vision (2), More Normal Freakers (7) and Unforgiving Standalone (22) are such downloads. Every entry names its interface symbol in `icon`.

`pinnedArchiveSha256` keeps an audited record when the published file is no longer on disk: Hidden Arsenal's local ZIP was changed after the published 1.3 upload, so its shipped record (archive `f2f46218…`) is reused instead of auditing the unpublished file. Remove the pin once the matching file is uploaded and the source points at it.

71 entries are installable and audited from the local release tree. Two existing published combined downloads are retained for identification and conflict reporting with `available:false` and `installationPolicy:blocked-combined`. Days Gone First Person (Beta) has `available:false` because its public artifact has not been matched locally, but `installationPolicy:review-required` allows a real official download to enter explicit local package review. Development candidates must not silently stand in for it.

This is the shipped, pinned catalog. The live Nexus inventory separately discovers this author's other Days Gone pages and new files without a toolkit update. Newly approved packages keep their official source identity and locally computed hash with `identityStatus:nexus-inspected`; that is distinct from an artifact audited into this catalog.

The generator reads the explicitly selected local ZIP or PAK in `scripts/catalog_sources.json`. For ZIPs, `packageFile`, `sha256`, `packageSize` and `assets` describe the actual PAK member; `archiveFile`, `archiveSha256` and `archiveSize` describe its container. Several public ZIPs use different PAK filenames than the loose development copies beside them. Do not infer installation filenames from development filenames.

Hashes prove the identity of the audited local artifact. They are not a claim that a live Nexus download has independently been fetched and matched. A new download with a different hash must be held for review. A version string of `unversioned` means the live file listing provided no version, even when a separate local note contains one. Package hashes remain the authoritative identity.

The two local PAK-only entries are Infinite Health and No Environmental Bike Damage. Their PAKs and asset manifests are audited; their release ZIPs are absent locally, so no archive hash is invented.

## What the conflict data establishes

Assets are normalized to lowercase, forward-slash, game-relative paths with Unreal's `../../../` mount prefix resolved. `assetOverlaps` records the exact shared paths for every pair with an overlap. The current inventory has 136 overlapping pairs and 2328 asset memberships across its 73 audited PAKs.

A shared cooked asset is a conflict because the game selects one complete version. Renaming a package or changing load order does not merge two versions. An overlap can remove only part of a mod's advertised behavior, so a package that still appears to work may remain in conflict.

No detected shared files means only that no direct file replacement overlap was found. It does not prove gameplay compatibility, independence of script behavior, support on another game build, or correctness of an unknown package. Empty assets on an unavailable entry mean **unknown**, never conflict-free.

Examples from the current artifacts:

- No Recoil and the two Infinite Ammo choices share 89 assets per pair.
- The Infinite Ammo choices share 93 assets with each other and are alternatives.
- No Recoil and Hidden Arsenal share 17 assets.
- XP Randomizer and No Recoil have no direct asset overlap and remain separate packages.
- XP Randomizer shares a player-controller asset with several input/toggle mods, including Noclip Toggle and Teleport To Map Marker.
- Each Longer Days choice replaces the same world map; select only one.

The older `release/mod-conflicts.txt` is not authoritative for this catalog. It predates newer mods and its statement that all disjoint packages can be freely combined is stronger than the available evidence.

## Regeneration

From the mod-manager directory:

```powershell
python scripts/catalog_generate.py
python scripts/catalog_generate.py --check
python scripts/catalog_test.py
```

The generator is deterministic and uses the standard library. It does not modify release packages, run builders, install mods, inspect saves, or copy game assets into this project. The regression check independently compares every manifest against the existing `tools/pak.py` reader.

Portable source builds set `DRIFTER_CATALOG_METADATA_ONLY=1`: three metadata/overlap checks run and exactly two real-artifact checks are explicitly skipped because release binaries and the parent workspace parser are not distributed. Running the commands above in this development workspace retains the full five-test verification.

The broader workspace contains unpublished tests, legacy duplicates, historical backups, other separately developed mods and explicit merged compatibility packages. Those are not promoted into this page's pinned catalog merely because files exist locally. First-person v24–v32 candidates, the camp-only test, optional gas-can balance experiments and the old No Camp Bike Speed Limit ZIP are examples. Adding an artifact to the pinned catalog requires its published source metadata and content audit; live discovery does not recursively treat local PAKs as published releases.
