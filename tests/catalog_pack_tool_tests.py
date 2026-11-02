"""Offline catalog pack adversarial fixtures; signs only ephemeral test keys."""
from __future__ import annotations

import contextlib
import copy
import hashlib
import io
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path
from unittest.mock import patch

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import catalog_pack as pack
import release_signing


def catalog():
    return {"schemaVersion": 1, "title": "Fixture", "mods": [{
        "id": "example", "name": "Example mod", "version": "1", "available": True,
        "packageFile": "Example_P.pak", "sha256": "a" * 64,
        "assets": ["bendgame/content/example.uasset"],
        "nexusUrl": "https://www.nexusmods.com/daysgone/mods/994?tab=files",
        "nexusModId": 994, "nexusFileId": 1, "packageSize": 10}]}


class CatalogPackTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="drifter-catalog-pack-test-")
        self.folder = Path(self.temporary.name)
        self.key = ec.generate_private_key(ec.SECP256R1())
        numbers = self.key.public_key().public_numbers()
        x, y = numbers.x.to_bytes(32, "big"), numbers.y.to_bytes(32, "big")
        self.identity = release_signing.key_id(x, y)
        self.public_path = self.folder / "test-public.json"
        self.private_path = self.folder / "test-private.pem"
        self.public_path.write_text(json.dumps({"curve": "P-256", "keyId": self.identity, "x": x.hex(), "y": y.hex()}), encoding="utf-8")
        self.private_path.write_bytes(self.key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        self.catalog_path = self.folder / "catalog.json"
        self.catalog_path.write_text(json.dumps(catalog()), encoding="utf-8")
        self.path = self.folder / "Drifters-Toolkit-Catalog-2.zip"

    def tearDown(self):
        self.temporary.cleanup()

    def sign(self, message):
        r, s = decode_dss_signature(self.key.sign(message, ec.ECDSA(hashes.SHA256())))
        return (r.to_bytes(32, "big") + s.to_bytes(32, "big")).hex()

    def record(self, raw, revision=2, minimum="1.0.4"):
        digest = hashlib.sha256(raw).hexdigest()
        return {"schemaVersion": 1, "kind": "catalog", "keyId": self.identity, "revision": revision,
                "minimumToolkitVersion": minimum, "catalogSha256": digest,
                "signature": self.sign(pack.signed_message(revision, minimum, digest))}

    def write_pack(self, document=None, raw=None, record=None, entries=None):
        raw = raw if raw is not None else json.dumps(document or catalog()).encode()
        record = record if record is not None else self.record(raw)
        entries = entries if entries is not None else [(pack.SIGNATURE_NAME, json.dumps(record).encode()), (pack.CATALOG_NAME, raw)]
        with warnings.catch_warnings(), zipfile.ZipFile(self.path, "w", zipfile.ZIP_DEFLATED) as archive:
            warnings.simplefilter("ignore", UserWarning)
            for name, value in entries:
                info = zipfile.ZipInfo(name)
                # ZipInfo normalizes Windows backslashes on construction. Restore
                # the original bytes to create an actual adversarial ZIP fixture.
                info.filename = name
                archive.writestr(info, value)
        return self.path

    def inspect(self, **kwargs):
        return pack.inspect(self.path, public_key_path=self.public_path, **kwargs)

    def test_build_and_verify_never_reads_the_real_signing_key(self):
        with patch.object(release_signing, "PRIVATE_KEY", self.folder / "not-created.pem"):
            result = pack.build(self.catalog_path, self.path, 2, "1.0.4", self.catalog_path, self.private_path, self.public_path)
            self.assertEqual(2, result["revision"])
            self.assertEqual(1, self.inspect(toolkit_version="1.0.4", current_revision=1)["modCount"])
        with zipfile.ZipFile(self.path) as archive:
            self.assertEqual({pack.SIGNATURE_NAME, pack.CATALOG_NAME}, set(archive.namelist()))

    def test_domain_and_message_are_exact(self):
        self.assertEqual(b"DriftersToolkit catalog pack\n1\n2\n1.0.4\n" + b"a" * 64 + b"\n", pack.signed_message(2, "1.0.4", "a" * 64))
        raw = json.dumps(catalog()).encode()
        record = self.record(raw)
        record["signature"] = self.sign(release_signing.message("windows", "1.0.4", raw))
        self.write_pack(raw=raw, record=record)
        with self.assertRaisesRegex(RuntimeError, "signature could not"):
            self.inspect()

    def test_character_preview_is_bundled_data_only(self):
        document = catalog()
        appearance = {"character": "deacon", "type": "outfit", "label": "Author preview",
                      "preview": "assets/mod-example.png"}
        document["mods"][0]["appearance"] = appearance
        pack.validate_catalog(document)
        for path in ("https://example.invalid/skin.png", "assets/../skin.png", "file:///skin.png",
                     "assets/mod-example.svg", "assets/mod-.png", "assets/mod-example.extra.png"):
            with self.subTest(path=path), self.assertRaises(RuntimeError):
                changed = copy.deepcopy(document)
                changed["mods"][0]["appearance"]["preview"] = path
                pack.validate_catalog(changed)
        injected = copy.deepcopy(document)
        injected["mods"][0]["appearance"]["exclusive"] = True
        with self.assertRaises(RuntimeError): pack.validate_catalog(injected)

    def test_tampered_catalog_and_signature_fail(self):
        raw = json.dumps(catalog()).encode()
        record = self.record(raw)
        self.write_pack(raw=raw + b" ", record=record)
        with self.assertRaisesRegex(RuntimeError, "checksum"):
            self.inspect()
        record["signature"] = "0" * 128
        self.write_pack(raw=raw, record=record)
        with self.assertRaisesRegex(RuntimeError, "signature could not"):
            self.inspect()

    def test_checksum_rewrite_does_not_resign_a_catalog(self):
        raw = json.dumps(catalog()).encode()
        record = self.record(raw)
        changed = raw + b"\n"
        record["catalogSha256"] = hashlib.sha256(changed).hexdigest()
        self.write_pack(raw=changed, record=record)
        with self.assertRaisesRegex(RuntimeError, "signature could not"):
            self.inspect()

    def test_signed_revision_and_minimum_version_cannot_be_rewritten(self):
        raw = json.dumps(catalog()).encode()
        for field, value in (("revision", 99), ("minimumToolkitVersion", "0.0.0")):
            with self.subTest(field=field):
                record = self.record(raw)
                record[field] = value
                self.write_pack(raw=raw, record=record)
                with self.assertRaisesRegex(RuntimeError, "signature could not"):
                    self.inspect()

    def test_minimum_version_and_rollback_are_enforced(self):
        self.write_pack()
        with self.assertRaisesRegex(RuntimeError, "newer version"):
            self.inspect(toolkit_version="1.0.3")
        self.assertEqual(2, self.inspect(toolkit_version="01.000.004")["revision"])
        for current in (2, 3):
            with self.subTest(current=current), self.assertRaisesRegex(RuntimeError, "already installed or older"):
                self.inspect(current_revision=current)

    def test_revision_and_version_format_bounds(self):
        for revision in (True, False, 0, -1, 1.5, pack.MAX_REVISION + 1, "2"):
            with self.subTest(revision=revision), self.assertRaises(RuntimeError):
                pack.signed_message(revision, "1.0.4", "a" * 64)
        for version in ("1.0", "1.0.4.0", "1.0.4\n", "1.0.4-beta", "1.0.1000000", None):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                pack.signed_message(2, version, "a" * 64)

    def test_exact_signature_schema_and_key_are_required(self):
        raw = json.dumps(catalog()).encode()
        for field, value in (("schemaVersion", True), ("kind", "release"), ("keyId", "unknown"), ("signature", "a" * 127), ("extra", 1)):
            with self.subTest(field=field):
                record = self.record(raw)
                record[field] = value
                self.write_pack(raw=raw, record=record)
                with self.assertRaises(RuntimeError):
                    self.inspect()

    def test_extra_duplicate_and_renamed_zip_entries_fail(self):
        raw = json.dumps(catalog()).encode()
        entries = [(pack.SIGNATURE_NAME, json.dumps(self.record(raw)).encode()), (pack.CATALOG_NAME, raw)]
        for changed in (entries + [("program.exe", b"x")], [entries[0], entries[0]],
                        [entries[0], ("../catalog/catalog.json", raw)],
                        [entries[0], ("catalog\\catalog.json", raw)], entries + [("catalog/", b"")]):
            with self.subTest(entries=[name for name, _ in changed]):
                self.write_pack(entries=changed)
                with self.assertRaisesRegex(RuntimeError, "exactly"):
                    self.inspect()

    def test_link_and_unsupported_compression_are_rejected(self):
        for mode, compression in ((stat.S_IFLNK | 0o777, zipfile.ZIP_STORED), (stat.S_IFREG | 0o644, zipfile.ZIP_BZIP2),
                                  (stat.S_IFREG | 0o755, zipfile.ZIP_STORED)):
            with self.subTest(mode=mode, compression=compression):
                with zipfile.ZipFile(self.path, "w") as archive:
                    archive.writestr(pack.SIGNATURE_NAME, b"{}")
                    info = zipfile.ZipInfo(pack.CATALOG_NAME)
                    info.external_attr = mode << 16
                    info.compress_type = compression
                    archive.writestr(info, b"{}")
                with self.assertRaises(RuntimeError):
                    self.inspect()

    def test_size_limits_apply_before_decompression_and_json(self):
        self.write_pack()
        for limit in ("MAX_PACK", "MAX_CATALOG", "MAX_SIGNATURE"):
            with self.subTest(limit=limit), patch.object(pack, limit, 4), self.assertRaisesRegex(RuntimeError, "size limit"):
                self.inspect()

    def test_duplicate_json_and_nonfinite_numbers_are_rejected(self):
        for raw in (b'{"schemaVersion":1,"schemaVersion":1,"mods":[]}', b'{"number":NaN}', b'{"number":1e9999}', b'\xff'):
            with self.subTest(raw=raw), self.assertRaises(RuntimeError):
                pack.parse_json(raw, 1000, "test")
        raw = json.dumps(catalog()).encode()
        record = json.dumps(self.record(raw)).encode().replace(b'"schemaVersion": 1', b'"schemaVersion": 1, "schemaVersion": 1')
        self.write_pack(entries=[(pack.CATALOG_NAME, raw), (pack.SIGNATURE_NAME, record)])
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            self.inspect()

    def test_unsafe_asset_paths_are_rejected_even_when_signed(self):
        for asset in ("../outside.uasset", "/outside.uasset", "x/../outside.uasset", "x\\asset.uasset", "X/asset.uasset", "x:asset", "x//asset", "x/\x7fasset"):
            with self.subTest(asset=asset):
                document = catalog()
                document["mods"][0]["assets"] = [asset]
                self.write_pack(document)
                with self.assertRaisesRegex(RuntimeError, "unsafe"):
                    self.inspect()

    def test_invalid_identity_flags_names_urls_and_sizes_fail(self):
        for field, value in (("id", "CON"), ("packageFile", "../other.pak"), ("packageFile", "BendGame-WindowsNoEditor.pak"),
                             ("sha256", "A" * 64), ("available", "false"), ("name", ""), ("name", "x" * 513),
                             ("nexusUrl", "https://www.nexusmods.com.evil.test/daysgone/mods/994"),
                             ("nexusUrl", "https://www.nexusmods.com/daysgone/mods/994?x=1"),
                             ("nexusModId", True), ("packageSize", -1), ("archiveFile", "../file.zip")):
            with self.subTest(field=field, value=value):
                document = catalog()
                document["mods"][0][field] = value
                with self.assertRaises(RuntimeError):
                    pack.validate_catalog(document)

    def test_duplicate_ids_and_ambiguous_filenames_fail(self):
        document = catalog()
        other = copy.deepcopy(document["mods"][0])
        other["id"] = "EXAMPLE"
        document["mods"].append(other)
        with self.assertRaisesRegex(RuntimeError, "duplicate package IDs"):
            pack.validate_catalog(document)
        other["id"] = "example-two"
        with self.assertRaisesRegex(RuntimeError, "ambiguous"):
            pack.validate_catalog(document)
        document["mods"][0]["variantGroup"] = other["variantGroup"] = "example-options"
        other["sha256"] = "b" * 64
        pack.validate_catalog(document)
        document["mods"][0]["assets"] *= 2
        with self.assertRaisesRegex(RuntimeError, "duplicate asset"):
            pack.validate_catalog(document)

    def test_structural_resource_bounds(self):
        document = catalog()
        document["extra"] = "x" * (pack.MAX_TEXT + 1)
        with self.assertRaisesRegex(RuntimeError, "too long"):
            pack.validate_catalog(document)
        document = catalog()
        item = document
        for _ in range(pack.MAX_DEPTH + 1):
            item["extra"] = {}
            item = item["extra"]
        with self.assertRaisesRegex(RuntimeError, "too complex"):
            pack.validate_catalog(document)
        with patch.object(pack, "MAX_MOD_ASSETS", 0), self.assertRaisesRegex(RuntimeError, "asset list"):
            pack.validate_catalog(catalog())
        with patch.object(pack, "MAX_ASSETS", 0), self.assertRaisesRegex(RuntimeError, "asset list"):
            pack.validate_catalog(catalog())

    def test_empty_unavailable_entry_and_future_metadata_are_supported(self):
        document = catalog()
        document["mods"][0].update(available=False, packageFile="", sha256="", assets=[])
        document["futureMetadata"] = {"text": "Data only", "list": [1, True, None]}
        pack.validate_catalog(document)

    def test_existing_ids_cannot_be_removed_or_reused(self):
        previous = catalog()
        for field, value in (("id", "new-name"), ("packageFile", "Other_P.pak"), ("sha256", "b" * 64),
                             ("assets", ["bendgame/content/other.uasset"])):
            with self.subTest(field=field):
                candidate = copy.deepcopy(previous)
                candidate["mods"][0][field] = value
                with self.assertRaises(RuntimeError):
                    pack.validate_transition(previous, candidate)
        candidate = copy.deepcopy(previous)
        candidate["mods"][0]["description"] = "Updated wording."
        newer = copy.deepcopy(candidate["mods"][0])
        newer.update(id="example-v2", packageFile="ExampleV2_P.pak", sha256="b" * 64)
        candidate["mods"].append(newer)
        pack.validate_transition(previous, candidate)

    def test_empty_previous_baseline_is_supported_but_signed_empty_catalogs_are_not(self):
        previous = {"schemaVersion": 1, "mods": []}
        pack.validate_transition(previous, catalog())
        with self.assertRaises(RuntimeError):
            pack.validate_catalog(previous)
        with self.assertRaises(RuntimeError):
            pack.validate_transition(catalog(), previous)
        previous_path = self.folder / "empty-previous.json"
        previous_path.write_text(json.dumps(previous), encoding="utf-8")
        result = pack.build(self.catalog_path, self.path, 2, "1.0.4", previous_path, self.private_path, self.public_path)
        self.assertEqual(1, result["modCount"])
        self.write_pack(previous)
        with self.assertRaisesRegex(RuntimeError, "between 1 and 4096"):
            self.inspect()

    def test_asset_reordering_preserves_identity_and_unicode_is_not_casefolded(self):
        previous = catalog()
        previous["mods"][0]["assets"].append("bendgame/content/second.uasset")
        candidate = copy.deepcopy(previous)
        candidate["mods"][0]["assets"].reverse()
        pack.validate_transition(previous, candidate)
        self.assertEqual("mod-Ä", pack.lower_ascii("MOD-Ä"))

    def test_replacement_release_chain_is_metadata_and_retains_old_identities(self):
        previous = catalog()
        document = copy.deepcopy(previous)
        second, third = copy.deepcopy(previous["mods"][0]), copy.deepcopy(previous["mods"][0])
        second.update(id="example-v2", packageFile="ExampleV2_P.pak", sha256="b" * 64, supersededBy="example-v3")
        third.update(id="example-v3", packageFile="ExampleV3_P.pak", sha256="c" * 64)
        document["mods"][0]["supersededBy"] = "example-v2"
        document["mods"].extend((second, third))
        pack.validate_catalog(document)
        pack.validate_transition(previous, document)

    def test_replacement_targets_must_identify_a_real_different_available_release(self):
        document = catalog()
        newer = copy.deepcopy(document["mods"][0])
        newer.update(id="example-v2", packageFile="ExampleV2_P.pak", sha256="b" * 64)
        document["mods"].append(newer)
        for target in ("example", "missing", "EXAMPLE-V2", "", "../example-v2", 2, {}):
            with self.subTest(target=target):
                candidate = copy.deepcopy(document)
                candidate["mods"][0]["supersededBy"] = target
                with self.assertRaisesRegex(RuntimeError, "replacement release"):
                    pack.validate_catalog(candidate)
        document["mods"][0]["supersededBy"] = "example-v2"
        newer["available"] = False
        with self.assertRaisesRegex(RuntimeError, "replacement release"):
            pack.validate_catalog(document)
        newer["available"] = True
        newer["sha256"] = document["mods"][0]["sha256"]
        with self.assertRaisesRegex(RuntimeError, "replacement release"):
            pack.validate_catalog(document)

    def test_replacement_cycles_fail_and_same_filename_versions_need_variant_groups(self):
        document = catalog()
        newer = copy.deepcopy(document["mods"][0])
        newer.update(id="example-v2", sha256="b" * 64)
        document["mods"].append(newer)
        document["mods"][0]["supersededBy"] = "example-v2"
        with self.assertRaisesRegex(RuntimeError, "ambiguous"):
            pack.validate_catalog(document)
        newer["variantGroup"] = document["mods"][0]["variantGroup"] = "example-releases"
        pack.validate_catalog(document)
        newer["supersededBy"] = "example"
        with self.assertRaisesRegex(RuntimeError, "cycle"):
            pack.validate_catalog(document)

    @unittest.skipUnless(os.environ.get("DRIFTER_CATALOG_NATIVE_TEST"), "Optional native catalog test binary was not supplied.")
    def test_python_built_pack_verifies_with_native_crypto_and_tampering_is_rejected(self):
        binary = Path(os.environ["DRIFTER_CATALOG_NATIVE_TEST"]).resolve()
        pack.build(self.catalog_path, self.path, 2, "1.0.4", self.catalog_path, self.private_path, self.public_path)
        command = [str(binary), "--inspect-tool-pack", str(self.path), str(self.public_path)]
        result = subprocess.run(command, text=True, capture_output=True, timeout=20)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertEqual({"revision": 2, "mods": 1}, json.loads(result.stdout))
        raw, signature = pack.read_pack(self.path)
        self.write_pack(raw=raw + b"\n", record=json.loads(signature))
        result = subprocess.run(command, text=True, capture_output=True, timeout=20)
        self.assertNotEqual(0, result.returncode, "Native verification accepted a changed catalog.")

    def test_build_will_not_overwrite_an_existing_pack(self):
        self.path.write_bytes(b"preserve me")
        with self.assertRaisesRegex(RuntimeError, "already exists"):
            pack.build(self.catalog_path, self.path, 2, "1.0.4", self.catalog_path, self.private_path, self.public_path)
        self.assertEqual(b"preserve me", self.path.read_bytes())

    def test_cli_inspect_is_verified_and_plain_errors_have_no_traceback(self):
        self.write_pack()
        output, errors = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            self.assertEqual(0, pack.main(["inspect", str(self.path), "--public-key", str(self.public_path)]))
        self.assertIn("Verified catalog revision 2", output.getvalue())
        self.path.write_bytes(b"not a zip")
        with contextlib.redirect_stderr(errors):
            self.assertEqual(1, pack.main(["verify", str(self.path), "--public-key", str(self.public_path)]))
        self.assertNotIn("Traceback", errors.getvalue())
        self.assertIn("not a readable ZIP", errors.getvalue())

    def test_current_shipped_catalog_is_accepted_without_regenerating_it(self):
        raw = (ROOT / "catalog" / "catalog.json").read_bytes()
        document = pack.parse_json(raw, pack.MAX_CATALOG, "catalog")
        pack.validate_catalog(document)
        self.assertGreater(len(document["mods"]), 0)


if __name__ == "__main__":
    unittest.main()
