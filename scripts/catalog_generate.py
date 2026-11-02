"""Audit local release packages and generate the public mod-manager catalog.

Only reads explicitly selected local packages. Never runs mod builders, installs
mods, reads saves, or downloads anything. Uses the same UE4 v3 index layout as
tools/pak.py, with bounded reads and explicit validation instead of assertions.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import itertools
import json
import posixpath
import struct
import sys
import zipfile
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent
SOURCE_FILE = Path(__file__).with_name("catalog_sources.json")
OUTPUT = PROJECT / "catalog" / "catalog.json"


def normalize_asset(mount: str, name: str) -> str:
    """Resolve an Unreal mount prefix to a canonical game-relative file path."""
    joined = posixpath.normpath(mount.replace("\\", "/") + "/" + name.replace("\\", "/"))
    while joined.startswith("../"):
        joined = joined[3:]
    if joined.startswith("/") or ":" in joined or joined in ("", ".", ".."):
        raise ValueError(f"Invalid mounted asset path: {mount!r} {name!r}")
    if ".." in joined.split("/"):
        raise ValueError(f"Unresolved asset traversal: {joined!r}")
    return joined.lower()


def read_exact(stream, size: int) -> bytes:
    if size < 0:
        raise ValueError("Negative read length")
    result = stream.read(size)
    if len(result) != size:
        raise ValueError("Truncated PAK index")
    return result


def read_value(stream, fmt: str):
    return struct.unpack("<" + fmt, read_exact(stream, struct.calcsize("<" + fmt)))


def read_string(stream) -> str:
    (length,) = read_value(stream, "i")
    if not 0 < length <= 65536:
        raise ValueError(f"Invalid PAK string length: {length}")
    data = read_exact(stream, length)
    if not data.endswith(b"\0"):
        raise ValueError("Unterminated PAK string")
    return data[:-1].decode("utf-8")


def assets_from_pak(data: bytes) -> list[str]:
    if len(data) < 44:
        raise ValueError("Truncated PAK footer")
    magic, version, offset, size = struct.unpack("<IIqq", data[-44:-20])
    if magic != 0x5A6F12E1 or version != 3:
        raise ValueError("Expected Days Gone UE4 v3 PAK")
    if offset < 0 or size < 0 or offset + size != len(data) - 44:
        raise ValueError("PAK index bounds do not match file length")
    index = data[offset:offset + size]
    if hashlib.sha1(index).digest() != data[-20:]:
        raise ValueError("PAK index checksum mismatch")
    stream = io.BytesIO(index)
    mount = read_string(stream)
    (count,) = read_value(stream, "i")
    if not 0 < count <= 100000:
        raise ValueError(f"Invalid PAK entry count: {count}")
    assets = []
    for _ in range(count):
        name = read_string(stream)
        header_start = stream.tell()
        file_offset, compressed_size, raw_size, method = read_value(stream, "qqqI")
        read_exact(stream, 20)  # Content SHA-1, recorded in the PAK index.
        if min(file_offset, compressed_size, raw_size) < 0 or file_offset >= offset:
            raise ValueError("PAK entry data bounds are invalid")
        if method:
            (block_count,) = read_value(stream, "i")
            if not 0 < block_count <= 100000:
                raise ValueError("Invalid compression block count")
            for _ in range(block_count):
                start, end = read_value(stream, "qq")
                if not 0 <= start <= end <= offset:
                    raise ValueError("Compression block bounds are invalid")
        flags, _ = read_value(stream, "BI")
        if flags:
            raise ValueError("Encrypted PAK entries are not supported")
        header_size = stream.tell() - header_start
        if file_offset + header_size + compressed_size > offset:
            raise ValueError("PAK entry extends into the index")
        if not method and compressed_size != raw_size:
            raise ValueError("Uncompressed PAK entry sizes disagree")
        assets.append(normalize_asset(mount, name))
    if stream.tell() != len(index):
        raise ValueError("Trailing data in PAK index")
    if len(set(assets)) != len(assets):
        raise ValueError("Duplicate normalized asset paths")
    return sorted(assets)


def local_source(relative: str) -> Path:
    candidate = (WORKSPACE / relative).resolve()
    try:
        candidate.relative_to(WORKSPACE.resolve())
    except ValueError as error:
        raise ValueError("Catalog source must remain inside the workspace") from error
    return candidate


def audit_source(entry: dict) -> dict:
    source = local_source(entry["source"])
    raw = source.read_bytes()
    if source.suffix.lower() == ".zip":
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            members = [member for member in archive.infolist() if member.filename.lower().endswith(".pak")]
            # A ZIP with several PAKs (tiers, optional extras, standalone pieces) names the one this entry is.
            if entry.get("member"):
                members = [m for m in members if posixpath.basename(m.filename.replace("\\", "/")) == entry["member"]]
            if len(members) != 1:
                raise ValueError(f"Expected exactly one PAK: {source.name} {entry.get('member', '')}")
            member = members[0]
            if member.file_size > 512 * 1024 * 1024:
                raise ValueError("Catalog PAK exceeds audit size limit")
            package_name = posixpath.basename(member.filename.replace("\\", "/"))
            pak_data = archive.read(member)
        archive_metadata = {
            "archiveFile": source.name,
            "archiveSha256": hashlib.sha256(raw).hexdigest(),
            "archiveSize": len(raw),
        }
    else:
        package_name, pak_data, archive_metadata = source.name, raw, {}
    return {
        "packageFile": package_name,
        "sha256": hashlib.sha256(pak_data).hexdigest(),
        "packageSize": len(pak_data),
        "assets": assets_from_pak(pak_data),
        "identityStatus": "local-release-audited",
        **archive_metadata,
    }


AUDIT_FIELDS = ("packageFile", "sha256", "packageSize", "assets", "identityStatus", "archiveFile", "archiveSha256", "archiveSize")


def pinned_audit(source_entry: dict) -> dict | None:
    # The published file of a pinned entry may be gone from disk (a newer, unpublished build replaced it). Then the
    # shipped audited record is reused as is, never re-audited from a different file.
    pinned = source_entry.get("pinnedArchiveSha256")
    if not pinned:
        return None
    source = local_source(source_entry["source"])
    if source.exists() and hashlib.sha256(source.read_bytes()).hexdigest() == pinned:
        return None
    previous = json.loads(OUTPUT.read_text(encoding="utf-8"))
    for mod in previous["mods"]:
        if mod["id"] == source_entry["id"] and mod.get("archiveSha256") == pinned:
            return {key: mod[key] for key in AUDIT_FIELDS if key in mod}
    raise ValueError(f"Pinned record for {source_entry['id']} is missing from the shipped catalog")


def build_catalog(registry: dict) -> dict:
    mods = []
    for source_entry in registry["mods"]:
        entry = {key: value for key, value in source_entry.items() if key not in ("source", "member", "pinnedArchiveSha256")}
        entry.setdefault("available", True)
        entry.setdefault("version", "unversioned")
        entry.setdefault("nexusUrl", registry["nexusUrl"])
        entry.setdefault("nexusModId", 994)
        entry["publication"] = source_entry.get("publication", {"listed": True, "observedDate": registry["observedDate"]})
        if "source" in source_entry:
            entry.update(pinned_audit(source_entry) or audit_source(source_entry))
        else:
            entry.update({"packageFile": "", "sha256": "", "assets": [], "identityStatus": "unverified-public-artifact"})
            if entry["available"]:
                raise ValueError("A supported mod requires an audited package")
        mods.append(entry)
    if len({mod["id"] for mod in mods}) != len(mods):
        raise ValueError("Duplicate catalog mod IDs")
    overlaps = []
    for left, right in itertools.combinations(mods, 2):
        shared = sorted(set(left["assets"]) & set(right["assets"]))
        if shared:
            overlaps.append({"mods": [left["id"], right["id"]], "assets": shared})
    return {
        "schemaVersion": 1,
        "title": "Drifter's Toolkit",
        "observedDate": max([registry["observedDate"], *(mod["publication"]["observedDate"] for mod in mods)]),
        "nexusUrl": registry["nexusUrl"],
        "compatibilityRule": "A shared asset is a conflict: load order picks one complete file. No shared assets means no detected file overlap; gameplay compatibility is unverified.",
        "artifactIdentityNote": "Hashes identify audited local release artifacts. The live Nexus listing supplies published names and versions; remote download hashes have not been independently verified.",
        "mods": mods,
        "assetOverlaps": overlaps,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Fail if the checked-in catalog differs from the audited sources.")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    registry = json.loads(SOURCE_FILE.read_text(encoding="utf-8"))
    catalog = build_catalog(registry)
    text = json.dumps(catalog, indent=2, ensure_ascii=False) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
            print("Catalog is stale; run scripts/catalog_generate.py", file=sys.stderr)
            return 1
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8", newline="\n")
    supported = sum(mod["available"] for mod in catalog["mods"])
    print(f"Catalog: {len(catalog['mods'])} public entries, {supported} supported, {len(catalog['assetOverlaps'])} asset-overlap pairs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
