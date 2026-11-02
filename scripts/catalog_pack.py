"""Build and verify signed, data-only catalog packs for Drifter's Toolkit.

Catalog packs contain exactly CATALOG-SIGNATURE.json and catalog/catalog.json.
They share the release public key, but use a separate signature domain and never
contain programs or game files. Only the explicit build command reads a private
key. Inspect and verify require only the public key.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import os
import re
import stat
import sys
import zipfile
import zlib
from pathlib import Path

import release_signing

SIGNATURE_NAME = "CATALOG-SIGNATURE.json"
CATALOG_NAME = "catalog/catalog.json"
MAX_PACK = 10 * 1024 * 1024
MAX_CATALOG = 8 * 1024 * 1024
MAX_SIGNATURE = 4096
MAX_REVISION = 9007199254740991
MAX_MODS = 4096
MAX_MOD_ASSETS = 65536
MAX_ASSETS = 250000
MAX_DEPTH = 32
MAX_NODES = 1000000
MAX_TEXT = 16 * 1024
SIGNATURE_FIELDS = {"schemaVersion", "kind", "keyId", "revision", "minimumToolkitVersion", "catalogSha256", "signature"}
HASH = re.compile(r"[0-9a-f]{64}\Z")
VERSION = re.compile(r"[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}\Z")
NEXUS_URL = re.compile(r"https://www\.nexusmods\.com/daysgone/mods/[1-9][0-9]{0,14}(?:\?tab=files)?\Z")


def fail(message):
    raise RuntimeError(message)


def integer(value, minimum=0, maximum=MAX_REVISION):
    return type(value) is int and minimum <= value <= maximum


def lower_ascii(value):
    # Native IDs and filenames fold ASCII only. Unicode spelling is retained.
    return value.translate(str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"))


def version_parts(version):
    if not isinstance(version, str) or not VERSION.fullmatch(version):
        fail("The toolkit version must contain three numeric parts, each at most six digits.")
    return tuple(map(int, version.split(".")))


def signed_message(revision, minimum_version, catalog_sha256):
    if not integer(revision, 1):
        fail("The catalog revision must be a positive safe integer.")
    version_parts(minimum_version)
    if not isinstance(catalog_sha256, str) or not HASH.fullmatch(catalog_sha256):
        fail("The catalog checksum is invalid.")
    return f"DriftersToolkit catalog pack\n1\n{revision}\n{minimum_version}\n{catalog_sha256}\n".encode("ascii")


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail("The JSON contains a duplicate field.")
        result[key] = value
    return result


def _bounded_tree(document):
    pending = [(document, 0)]
    nodes = 0
    while pending:
        value, depth = pending.pop()
        # Objects/arrays emit both an opening and closing parse event in the
        # native bounded JSON reader; keys and scalar values each emit one.
        nodes += 2 if isinstance(value, (dict, list)) else 1
        if depth > MAX_DEPTH or nodes > MAX_NODES:
            fail("The catalog JSON is too complex.")
        if isinstance(value, str):
            if len(value.encode("utf-8")) > MAX_TEXT or "\x00" in value:
                fail("A catalog text field is too long or contains an invalid character.")
        elif isinstance(value, dict):
            pending.extend((part, depth + 1) for pair in value.items() for part in pair)
        elif isinstance(value, list):
            pending.extend((part, depth + 1) for part in value)
        elif isinstance(value, float) and not math.isfinite(value):
            fail("Non-finite numbers are not allowed in catalog JSON.")


def parse_json(data, limit, label):
    if not data or len(data) > limit:
        fail(f"The {label} exceeds its size limit or is empty.")
    try:
        document = json.loads(data.decode("utf-8"), object_pairs_hook=_unique_object,
                              parse_constant=lambda _: fail("Non-finite numbers are not allowed in catalog JSON."))
        _bounded_tree(document)
    except (UnicodeError, ValueError, RecursionError) as error:
        raise RuntimeError(f"The {label} is not valid UTF-8 JSON.") from error
    return document


def plain_name(name):
    if not isinstance(name, str) or not name or len(name.encode("utf-8")) > 180 or name in (".", ".."):
        return False
    if name[-1] in ". " or any(ord(c) < 32 or ord(c) == 127 or c in '<>:"/\\|?*' for c in name):
        return False
    base = lower_ascii(name.split(".", 1)[0])
    return base not in {"con", "prn", "aux", "nul"} and not re.fullmatch(r"(?:com|lpt)[1-9]", base)


def asset_path(value):
    if not isinstance(value, str) or not value or len(value.encode("utf-8")) > 1024:
        return False
    if any(ord(c) < 32 or 0x7f <= ord(c) <= 0x9f for c in value):
        return False
    if "\\" in value or ":" in value or any("A" <= c <= "Z" for c in value):
        return False
    return all(segment not in ("", ".", "..") for segment in value.split("/"))


def validate_catalog(document, *, allow_empty=False):
    """Validate signed metadata before it can become a trusted author catalog."""
    _bounded_tree(document)
    if not isinstance(document, dict) or type(document.get("schemaVersion")) is not int or document["schemaVersion"] != 1:
        fail("The catalog has an unsupported schema.")
    mods = document.get("mods")
    if not isinstance(mods, list) or not (0 if allow_empty else 1) <= len(mods) <= MAX_MODS:
        fail("The catalog must contain between 1 and 4096 mods, except for an empty previous baseline.")
    ids, filenames, total_assets = set(), {}, 0
    text_fields = ("name", "version", "category", "summary", "description", "icon", "nexusUrl", "identityStatus",
                   "installationPolicy", "unsupportedReason", "localVersion", "download")
    for mod in mods:
        if not isinstance(mod, dict) or not plain_name(mod.get("id")):
            fail("The catalog contains an invalid package ID.")
        identity = lower_ascii(mod["id"])
        if identity in ids:
            fail("The catalog contains duplicate package IDs.")
        ids.add(identity)
        for field in text_fields:
            if field in mod and not isinstance(mod[field], str):
                fail("The catalog contains an invalid text field.")
        if not mod.get("name") or len(mod["name"].encode("utf-8")) > 512:
            fail("The catalog contains an invalid mod name.")
        if "available" in mod and type(mod["available"]) is not bool:
            fail("The catalog contains an invalid availability flag.")
        filename, digest = mod.get("packageFile"), mod.get("sha256")
        if not isinstance(filename, str) or not isinstance(digest, str):
            fail("The catalog contains an invalid package record.")
        has_identity = bool(filename or digest)
        if mod.get("available", True) or has_identity:
            if not plain_name(filename) or not lower_ascii(filename).endswith(".pak") or not HASH.fullmatch(digest):
                fail("The catalog contains an invalid package record.")
        if lower_ascii(filename).startswith("bendgame-windowsnoeditor"):
            fail("The catalog cannot manage a base-game package.")
        if "appearance" in mod:
            look = mod["appearance"]
            if (not isinstance(look, dict) or set(look) != {"character", "type", "label", "preview"}
                    or look["character"] != "deacon" or look["type"] != "outfit"
                    or not isinstance(look["label"], str) or not 1 <= len(look["label"].encode("utf-8")) <= 180
                    or not isinstance(look["preview"], str) or len(look["preview"]) > 180
                    or not re.fullmatch(r"assets/mod-[A-Za-z0-9_-]+\.(?:png|jpg|webp)", look["preview"])):
                fail("The catalog contains invalid character preview metadata.")
        group = mod.get("variantGroup", "")
        if not isinstance(group, str) or len(group.encode("utf-8")) > 180:
            fail("The catalog contains an invalid variant group.")
        if filename:
            previous = filenames.setdefault(lower_ascii(filename), [])
            if any(not group or item.get("variantGroup", "") != group or item["sha256"] == digest for item in previous):
                fail("The catalog contains ambiguous package filenames.")
            previous.append(mod)
        assets = mod.get("assets")
        if not isinstance(assets, list) or len(assets) > MAX_MOD_ASSETS or ((mod.get("available", True) or has_identity) and not assets):
            fail("The catalog contains an invalid asset list.")
        total_assets += len(assets)
        if total_assets > MAX_ASSETS or any(not asset_path(asset) for asset in assets):
            fail("The catalog contains an unsafe or oversized asset list.")
        if len(set(assets)) != len(assets):
            fail("The catalog contains duplicate asset paths.")
        for field in ("sha256", "archiveSha256", "pinnedArchiveSha256"):
            if field in mod and (not isinstance(mod[field], str) or (mod[field] and not HASH.fullmatch(mod[field]))):
                fail("The catalog contains an invalid file checksum.")
        if "archiveFile" in mod and mod["archiveFile"] != "" and not plain_name(mod["archiveFile"]):
            fail("The catalog contains an unsafe archive filename.")
        for field in ("nexusModId", "nexusFileId"):
            if field in mod and not integer(mod[field], 1):
                fail("The catalog contains an invalid Nexus file or mod ID.")
        for field in ("packageSize", "archiveSize"):
            if field in mod and not integer(mod[field]):
                fail("The catalog contains an invalid file size.")
    by_id = {mod["id"]: mod for mod in mods}
    replacements = {}
    for mod in mods:
        if "supersededBy" not in mod:
            continue
        target_id = mod["supersededBy"]
        target = by_id.get(target_id) if isinstance(target_id, str) else None
        if not plain_name(target_id) or target is None or target_id == mod["id"] or not target.get("available", True) or target["sha256"] == mod["sha256"]:
            fail("The catalog contains an invalid replacement release.")
        replacements[mod["id"]] = target_id
    # Each release has at most one successor. Mark completed paths so every
    # edge is visited once, even when many old releases share a newer target.
    colors = {}
    for start in replacements:
        path, current = [], start
        while current in replacements and colors.get(current, 0) == 0:
            colors[current] = 1
            path.append(current)
            current = replacements[current]
        if colors.get(current) == 1:
            fail("The catalog contains a cycle in replacement releases.")
        for identity in path:
            colors[identity] = 2
    # URLs are metadata, but only the intended Nexus game pages are linkable.
    pending = [document]
    while pending:
        item = pending.pop()
        if isinstance(item, dict):
            for key, value in item.items():
                if key == "nexusUrl" and (not isinstance(value, str) or not NEXUS_URL.fullmatch(value)):
                    fail("The catalog contains an invalid Nexus page URL.")
                pending.append(value)
        elif isinstance(item, list):
            pending.extend(item)
    return document


def validate_transition(previous, candidate):
    """Do not strand installed mods by removing or reusing their trusted IDs."""
    # Older fixture/offline builds can ship an empty initial catalog. This
    # exception never applies to the new signed catalog being published.
    validate_catalog(previous, allow_empty=True)
    validate_catalog(candidate)
    next_mods = {lower_ascii(mod["id"]): mod for mod in candidate["mods"]}
    for mod in previous["mods"]:
        next_mod = next_mods.get(lower_ascii(mod["id"]))
        if next_mod is None:
            fail("The new catalog removes an existing mod ID. Keep previous releases in the catalog.")
        if any(next_mod.get(field) != mod.get(field) for field in ("id", "packageFile", "sha256")) or set(next_mod["assets"]) != set(mod["assets"]):
            fail("The new catalog changes an existing mod's identity. Give new versions a new mod ID.")


def read_bounded(path, maximum, label):
    with Path(path).open("rb") as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            fail(f"The {label} must be a regular file.")
        data = source.read(maximum + 1)
    if not data or len(data) > maximum:
        fail(f"The {label} exceeds its size limit or is empty.")
    return data


def read_pack(path):
    raw = read_bounded(path, MAX_PACK, "catalog pack")
    try:
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            infos = archive.infolist()
            if len(infos) != 2 or {info.orig_filename for info in infos} != {SIGNATURE_NAME, CATALOG_NAME}:
                fail("A catalog pack must contain exactly its signature and catalog JSON.")
            files = {}
            for info in infos:
                mode = info.external_attr >> 16
                limit = MAX_CATALOG if info.filename == CATALOG_NAME else MAX_SIGNATURE
                if info.is_dir() or stat.S_IFMT(mode) not in (0, stat.S_IFREG) or mode & 0o111 or info.flag_bits & 1:
                    fail("The catalog pack contains a link, directory, executable or encrypted entry.")
                if info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED) or info.file_size > limit:
                    fail("A catalog pack entry exceeds its size limit or uses unsupported compression.")
                with archive.open(info) as source:
                    data = source.read(limit + 1)
                if len(data) != info.file_size or len(data) > limit:
                    fail("A catalog pack entry has an invalid size.")
                files[info.filename] = data
    except (zipfile.BadZipFile, NotImplementedError, EOFError, zlib.error) as error:
        raise RuntimeError("The catalog pack is not a readable ZIP.") from error
    return files[CATALOG_NAME], files[SIGNATURE_NAME]


def public_key(path=None):
    # Reuse the release signer's public key reader without reading private data.
    if path is None:
        try:
            return release_signing.public_key()
        except (KeyError, TypeError, ValueError) as error:
            raise RuntimeError("The catalog public key is invalid.") from error
    data = parse_json(read_bounded(path, MAX_SIGNATURE, "public key"), MAX_SIGNATURE, "public key")
    try:
        x, y = bytes.fromhex(data["x"]), bytes.fromhex(data["y"])
        if data.get("curve") != "P-256" or len(x) != 32 or len(y) != 32 or data["keyId"] != release_signing.key_id(x, y):
            fail("The catalog public key is invalid.")
        return x, y, data["keyId"]
    except (KeyError, TypeError, ValueError) as error:
        raise RuntimeError("The catalog public key is invalid.") from error


def inspect(path, toolkit_version=None, current_revision=None, public_key_path=None):
    catalog_bytes, record_bytes = read_pack(path)
    record = parse_json(record_bytes, MAX_SIGNATURE, "catalog signature")
    if not isinstance(record, dict) or set(record) != SIGNATURE_FIELDS or type(record["schemaVersion"]) is not int or record["schemaVersion"] != 1 or record["kind"] != "catalog":
        fail("The catalog signature record is malformed.")
    message = signed_message(record["revision"], record["minimumToolkitVersion"], record["catalogSha256"])
    if hashlib.sha256(catalog_bytes).hexdigest() != record["catalogSha256"]:
        fail("The catalog does not match its signed checksum.")
    x, y, identity = public_key(public_key_path)
    if record["keyId"] != identity:
        fail("The catalog was signed with an unknown key.")
    signature = record["signature"]
    if not isinstance(signature, str) or not re.fullmatch(r"[0-9a-f]{128}", signature):
        fail("The catalog signature has an invalid format.")
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    try:
        raw = bytes.fromhex(signature)
        key = ec.EllipticCurvePublicNumbers(int.from_bytes(x, "big"), int.from_bytes(y, "big"), ec.SECP256R1()).public_key()
        der = encode_dss_signature(int.from_bytes(raw[:32], "big"), int.from_bytes(raw[32:], "big"))
        key.verify(der, message, ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, ValueError) as error:
        raise RuntimeError("The catalog signature could not be verified.") from error
    document = validate_catalog(parse_json(catalog_bytes, MAX_CATALOG, "catalog"))
    if toolkit_version is not None and version_parts(toolkit_version) < version_parts(record["minimumToolkitVersion"]):
        fail("This catalog needs a newer version of the toolkit.")
    if current_revision is not None:
        if not integer(current_revision):
            fail("The installed catalog revision is invalid.")
        if record["revision"] <= current_revision:
            fail("This catalog revision is already installed or older.")
    return {**record, "modCount": len(document["mods"])}


def build(catalog_path, output, revision, minimum_version, previous_catalog_path, private_key_path=None, public_key_path=None):
    catalog_bytes = read_bounded(catalog_path, MAX_CATALOG, "catalog")
    document = validate_catalog(parse_json(catalog_bytes, MAX_CATALOG, "catalog"))
    previous = parse_json(read_bounded(previous_catalog_path, MAX_CATALOG, "previous catalog"), MAX_CATALOG, "previous catalog")
    validate_transition(previous, document)
    digest = hashlib.sha256(catalog_bytes).hexdigest()
    message = signed_message(revision, minimum_version, digest)
    output = Path(output)
    if output.exists():
        fail("The catalog pack already exists. Choose a new filename.")
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    # This is the only private-key read in this module. Never run it as part of inspection.
    key = serialization.load_pem_private_key(Path(private_key_path or release_signing.PRIVATE_KEY).read_bytes(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1):
        fail("A P-256 private key is required.")
    x, y, identity = public_key(public_key_path)
    numbers = key.public_key().public_numbers()
    if (numbers.x.to_bytes(32, "big"), numbers.y.to_bytes(32, "big")) != (x, y):
        fail("The private key does not match the catalog public key.")
    r, s = decode_dss_signature(key.sign(message, ec.ECDSA(hashes.SHA256())))
    record = {"schemaVersion": 1, "kind": "catalog", "keyId": identity, "revision": revision,
              "minimumToolkitVersion": minimum_version, "catalogSha256": digest,
              "signature": (r.to_bytes(32, "big") + s.to_bytes(32, "big")).hex()}
    packed = io.BytesIO()
    with zipfile.ZipFile(packed, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in ((SIGNATURE_NAME, (json.dumps(record, indent=2) + "\n").encode()), (CATALOG_NAME, catalog_bytes)):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(info, data)
    if len(packed.getvalue()) > MAX_PACK:
        fail("The catalog pack exceeds its size limit.")
    with output.open("xb") as destination:
        destination.write(packed.getvalue())
    return inspect(output, public_key_path=public_key_path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    make = commands.add_parser("build", help="Validate and sign a new catalog pack (reads the private key).")
    make.add_argument("catalog", type=Path)
    make.add_argument("--revision", required=True, type=int)
    make.add_argument("--previous-catalog", required=True, type=Path,
                      help="Previous published catalog. Existing IDs and package identity must be retained.")
    make.add_argument("--minimum-toolkit-version", default=release_signing.app_version())
    make.add_argument("--output", type=Path, help="Defaults to Drifters-Toolkit-Catalog-<revision>.zip in the current folder.")
    make.add_argument("--private-key", type=Path)
    make.add_argument("--public-key", type=Path)
    for name in ("inspect", "verify"):
        command = commands.add_parser(name, help="Verify signature, catalog and toolkit compatibility; no private key needed.")
        command.add_argument("pack", type=Path)
        command.add_argument("--toolkit-version", default=release_signing.app_version())
        command.add_argument("--current-revision", type=int)
        command.add_argument("--public-key", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            output = args.output or Path(f"Drifters-Toolkit-Catalog-{args.revision}.zip")
            result = build(args.catalog, output, args.revision, args.minimum_toolkit_version,
                           args.previous_catalog, args.private_key, args.public_key)
            print(f"Built catalog revision {result['revision']} ({result['modCount']} mods): {output.name}")
        else:
            result = inspect(args.pack, args.toolkit_version, args.current_revision, args.public_key)
            print(f"Verified catalog revision {result['revision']} ({result['modCount']} mods; toolkit {result['minimumToolkitVersion']} or later).")
        return 0
    except (RuntimeError, OSError, ValueError, ImportError) as error:
        print(f"Catalog pack: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
