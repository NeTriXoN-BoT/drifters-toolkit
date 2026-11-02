"""Catalog identity and overlap regression checks; no game or save mutations."""
from __future__ import annotations

import hashlib
import itertools
import json
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

import catalog_generate as catalog


class CatalogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(catalog.OUTPUT.read_text(encoding="utf-8"))
        cls.registry = json.loads(catalog.SOURCE_FILE.read_text(encoding="utf-8"))
        cls.mods = {mod["id"]: mod for mod in cls.data["mods"]}

    def test_public_inventory_and_download_identity(self):
        self.assertEqual(80, len(self.mods))
        self.assertEqual(78, sum(mod["available"] for mod in self.mods.values()))
        # 52 downloads; multi-PAK files list each individual PAK under one file ID.
        self.assertEqual(52, len({mod["nexusFileId"] for mod in self.mods.values()}))
        self.assertEqual(3279, self.mods["skill-respec"]["nexusFileId"])
        self.assertEqual(3286, self.mods["art-the-clown-player-model"]["nexusFileId"])
        self.assertEqual("9c61f848900e4470e032b87ca5ec83d9159ba9a5ccf6414b25aaf48f9122b805", self.mods["skill-respec"]["archiveSha256"])
        self.assertEqual("73a9bf9272502542fc4ad34b37221c2c77a4ed6b667a973d3d42a02b7bd04610", self.mods["art-the-clown-player-model"]["archiveSha256"])
        self.assertFalse(any(mod.get("installationPolicy") == "blocked-combined" for mod in self.mods.values()))
        for mod in self.mods.values():
            with self.subTest(mod=mod["id"]):
                self.assertEqual(994, mod["nexusModId"])
                self.assertNotIn("source", mod)
                self.assertNotIn("sourcePath", mod)
                if mod["available"]:
                    self.assertRegex(mod["sha256"], r"^[0-9a-f]{64}$")
                    self.assertTrue(mod["assets"])
                    self.assertTrue(mod["packageFile"].endswith(".pak"))
                else:
                    self.assertTrue(mod["unsupportedReason"])

    def test_latest_published_packages_and_manual_native_limits(self):
        expected = {
            "bike-wheelie": (3299, "fbcf4970577190796a155038e0d651b91b838a0dc532bdc0ab1716b44c5aae12", 61262),
            "headshot-only-freakers": (3298, "db83751e2ac95a6554e73c4669f86e9922b3ac2dc0e50d6b6132610a5893a0f9", 955762),
            "ragdoll-deaths": (3301, "206fc2db15c95f5bb0c9c267fc8a6d3f946e2a552c5d2dd0247d7f151a4baa29", 7028),
            "bleed-out": (3302, "df5f71539879b0891c0fdb920540ff85e2dae21f14268d397e59b9b91827df0f", 918720),
        }
        for mod_id, (file_id, archive_hash, archive_size) in expected.items():
            with self.subTest(mod=mod_id):
                mod = self.mods[mod_id]
                self.assertEqual(file_id, mod["nexusFileId"])
                self.assertEqual(archive_hash, mod["archiveSha256"])
                self.assertEqual(archive_size, mod["archiveSize"])
                self.assertEqual("1", mod["version"])
                self.assertEqual({"listed": True, "observedDate": "2026-10-30"}, mod["publication"])
                self.assertTrue(mod["icon"])
        native = self.mods["bleed-out"]
        self.assertFalse(native["available"])
        self.assertEqual("manual-native", native["installationPolicy"])
        self.assertEqual("", native["packageFile"])
        self.assertEqual("", native["sha256"])
        self.assertEqual([], native["assets"])
        self.assertIn("manual", native["unsupportedReason"].lower())
        self.assertIn("profiles", native["compatibilityNotes"])
        self.assertFalse(any("bleed-out" in pair["mods"] for pair in self.data["assetOverlaps"]))
        headshot = self.mods["headshot-only-freakers"]
        self.assertTrue(headshot["available"])
        self.assertEqual("HeadshotOnlyFreakers_P.pak", headshot["packageFile"])
        self.assertTrue(any("ASI" in note and "does not" in note for note in headshot["notes"]))
        self.assertTrue(any(set(pair["mods"]) == {"bike-wheelie", "manual-bike-headlight"}
                            for pair in self.data["assetOverlaps"]))

    def test_existing_published_combined_downloads_are_manageable(self):
        # Mod-building policy must not exclude already-published author packages
        # from ordinary import/management. Their audited identities stay fixed.
        cases = (
            ("noclip-no-detection-combined", 3172,
             "8142fec2aa612fd6aa1a2775ce7b93fed53f6ff80ef65e7b13ca640341e35bfd",
             "b8db9d29ea2b21bd79ca2428e6dab49e15d4ef35b6bd237d8e07c117ca1b669a",
             ("noclip-toggle", "no-enemy-detection-toggle")),
            ("bike-fuel-no-ejection-combined", 3158,
             "5a015a94ea41f21ba7be9002837973af4857761ce913acda713373c31c3628e8",
             "b0dda9ee633088b482d7beb3b9079d8a14c6be9c8df10bdfdc843e7b01177178",
             ("infinite-bike-fuel", "no-bike-crash-ejection")),
        )
        for mod_id, file_id, archive_hash, package_hash, alternatives in cases:
            with self.subTest(mod=mod_id):
                mod = self.mods[mod_id]
                self.assertTrue(mod["available"])
                self.assertNotIn("installationPolicy", mod)
                self.assertNotIn("unsupportedReason", mod)
                self.assertEqual(file_id, mod["nexusFileId"])
                self.assertEqual(archive_hash, mod["archiveSha256"])
                self.assertEqual(package_hash, mod["sha256"])
                for alternative in alternatives:
                    shared = sorted(set(mod["assets"]) & set(self.mods[alternative]["assets"]))
                    self.assertTrue(shared, f"Missing overlap with {alternative}")
                    self.assertTrue(any(set(pair["mods"]) == {mod_id, alternative} and pair["assets"] == shared
                                        for pair in self.data["assetOverlaps"]))

    @unittest.skipIf(os.environ.get("DRIFTER_CATALOG_METADATA_ONLY") == "1", "Real published artifacts and the workspace PAK reader are intentionally excluded from the portable source archive.")
    def test_assets_agree_with_existing_workspace_pak_reader(self):
        # Cross-check every real artifact against the established parser; never
        # decompress assets or run builders that might install game packages.
        sys.path.insert(0, str(catalog.WORKSPACE / "tools"))
        from pak import Pak
        with tempfile.TemporaryDirectory(prefix="drifter-catalog-") as temporary:
            temporary_pak = Path(temporary) / "audit.pak"
            for entry in self.registry["mods"]:
                if "source" not in entry:
                    continue
                with self.subTest(mod=entry["id"]):
                    source = catalog.local_source(entry["source"])
                    if catalog.pinned_audit(entry):
                        # The published file is no longer on disk; its shipped record is kept as is.
                        self.assertEqual(entry["pinnedArchiveSha256"], self.mods[entry["id"]]["archiveSha256"])
                        continue
                    if source.suffix.lower() == ".zip":
                        with zipfile.ZipFile(source) as archive:
                            member = next(name for name in archive.namelist() if name.lower().endswith(".pak") and (not entry.get("member") or name.split("/")[-1] == entry["member"]))
                            raw = archive.read(member)
                        temporary_pak.write_bytes(raw)
                        path = temporary_pak
                    else:
                        raw, path = source.read_bytes(), source
                    self.assertEqual(hashlib.sha256(raw).hexdigest(), self.mods[entry["id"]]["sha256"])
                    pak = Pak(path)
                    try:
                        expected = sorted(catalog.normalize_asset(pak.mount, name) for name in pak.entries)
                        self.assertEqual(expected, self.mods[entry["id"]]["assets"])
                    finally:
                        pak.f.close()

    def test_overlap_evidence_is_exhaustive(self):
        expected = {}
        for left, right in itertools.combinations(self.data["mods"], 2):
            shared = sorted(set(left["assets"]) & set(right["assets"]))
            if shared:
                expected[tuple([left["id"], right["id"]])] = shared
        actual = {tuple(pair["mods"]): pair["assets"] for pair in self.data["assetOverlaps"]}
        self.assertEqual(expected, actual)
        self.assertFalse(set(self.mods["xp-randomizer"]["assets"]) & set(self.mods["no-recoil"]["assets"]))
        self.assertEqual(89, len(set(self.mods["no-recoil"]["assets"]) & set(self.mods["infinite-ammo-no-reload"]["assets"])))
        self.assertEqual(93, len(set(self.mods["infinite-ammo-with-reload"]["assets"]) & set(self.mods["infinite-ammo-no-reload"]["assets"])))

    def test_canonical_paths_and_unavailable_beta(self):
        for mod in self.mods.values():
            self.assertEqual(sorted(set(mod["assets"])), mod["assets"])
            for asset in mod["assets"]:
                self.assertEqual(asset.lower(), asset)
                self.assertNotIn("\\", asset)
                self.assertNotIn("..", asset.split("/"))
                self.assertFalse(asset.startswith("/"))
        beta = self.mods["first-person-beta"]
        self.assertFalse(beta["available"])
        self.assertEqual([], beta["assets"])
        self.assertEqual("", beta["sha256"])
        self.assertEqual("review-required", beta["installationPolicy"])

    @unittest.skipIf(os.environ.get("DRIFTER_CATALOG_METADATA_ONLY") == "1", "Real published artifacts are intentionally excluded from the portable source archive.")
    def test_damaged_index_is_rejected(self):
        source = next(entry for entry in self.registry["mods"] if entry["id"] == "no-recoil")
        with zipfile.ZipFile(catalog.local_source(source["source"])) as archive:
            data = archive.read("NoRecoil_P.pak")
        damaged = bytearray(data)
        damaged[-45] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            catalog.assets_from_pak(bytes(damaged))
        with self.assertRaisesRegex(ValueError, "footer"):
            catalog.assets_from_pak(data[:20])
        with self.assertRaisesRegex(ValueError, "inside the workspace"):
            catalog.local_source("../../outside.pak")


if __name__ == "__main__":
    unittest.main()
