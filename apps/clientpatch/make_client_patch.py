#!/usr/bin/env python3
"""Build a WoW 3.3.5a client patch MPQ carrying an extended Item.dbc.

Appends rows for custom items to a base Item.dbc (WDBC format) and packs the
result into a locale patch archive (MPQ format v0, files stored uncompressed
as single units) that the client loads from Data/<locale>/.

Usage:
    python make_client_patch.py <base_Item.dbc> <rows.csv> <out_patch.mpq>

rows.csv columns (one row per item, header required):
    entry,class,subclass,sound_override,material,displayid,inventory_type,sheath
"""

import csv
import struct
import sys

DBC_MAGIC = b"WDBC"
ITEM_DBC_FIELDS = 8
ITEM_DBC_RECORD_SIZE = 32

MPQ_MAGIC = b"MPQ\x1a"
MPQ_HEADER_SIZE = 0x20
MPQ_HASH_ENTRY_EMPTY = 0xFFFFFFFF
MPQ_FILE_EXISTS = 0x80000000
MPQ_FILE_SINGLE_UNIT = 0x01000000

MASK32 = 0xFFFFFFFF


def build_crypt_table():
    table = [0] * 0x500
    seed = 0x00100001
    for index1 in range(0x100):
        index2 = index1
        for _ in range(5):
            seed = (seed * 125 + 3) % 0x2AAAAB
            temp1 = (seed & 0xFFFF) << 0x10
            seed = (seed * 125 + 3) % 0x2AAAAB
            temp2 = seed & 0xFFFF
            table[index2] = temp1 | temp2
            index2 += 0x100
    return table


CRYPT_TABLE = build_crypt_table()

HASH_TABLE_OFFSET = 0
HASH_NAME_A = 1
HASH_NAME_B = 2
HASH_FILE_KEY = 3


def hash_string(name, hash_type):
    seed1 = 0x7FED7FED
    seed2 = 0xEEEEEEEE
    for ch in name.upper():
        value = CRYPT_TABLE[(hash_type << 8) + ord(ch)]
        seed1 = (value ^ ((seed1 + seed2) & MASK32)) & MASK32
        seed2 = (ord(ch) + seed1 + seed2 + (seed2 << 5) + 3) & MASK32
    return seed1


def encrypt_block(data, key):
    seed = 0xEEEEEEEE
    result = bytearray()
    for (value,) in struct.iter_unpack("<I", data):
        seed = (seed + CRYPT_TABLE[0x400 + (key & 0xFF)]) & MASK32
        encrypted = value ^ ((key + seed) & MASK32)
        result += struct.pack("<I", encrypted)
        key = (((~key << 0x15) + 0x11111111) | (key >> 0x0B)) & MASK32
        seed = (value + seed + (seed << 5) + 3) & MASK32
    return bytes(result)


def append_item_dbc_rows(dbc_bytes, rows):
    magic, record_count, field_count, record_size, string_block_size = struct.unpack_from("<4s4I", dbc_bytes)
    if magic != DBC_MAGIC:
        raise SystemExit("base file is not a WDBC file")
    if field_count != ITEM_DBC_FIELDS or record_size != ITEM_DBC_RECORD_SIZE:
        raise SystemExit(f"unexpected Item.dbc layout: {field_count} fields, {record_size}-byte records")

    records_end = 20 + record_count * record_size
    existing_ids = {struct.unpack_from("<I", dbc_bytes, 20 + i * record_size)[0] for i in range(record_count)}

    new_records = bytearray()
    for row in rows:
        entry = int(row["entry"])
        if entry in existing_ids:
            raise SystemExit(f"entry {entry} already present in base Item.dbc")
        new_records += struct.pack(
            "<IIIiIIII",
            entry,
            int(row["class"]),
            int(row["subclass"]),
            int(row["sound_override"]),
            int(row["material"]),
            int(row["displayid"]),
            int(row["inventory_type"]),
            int(row["sheath"]),
        )

    header = struct.pack("<4s4I", DBC_MAGIC, record_count + len(rows), field_count, record_size, string_block_size)
    return header + dbc_bytes[20:records_end] + new_records + dbc_bytes[records_end:]


def write_mpq(out_path, files):
    """files: list of (archive_path, bytes). Stored uncompressed, single unit."""
    files = files + [("(listfile)", "\r\n".join(path for path, _ in files).encode() + b"\r\n")]

    hash_table_size = 16
    while hash_table_size < len(files) * 2:
        hash_table_size *= 2

    file_data = bytearray()
    block_entries = []
    placements = []
    for path, content in files:
        placements.append((path, MPQ_HEADER_SIZE + len(file_data), len(content)))
        file_data += content

    hash_entries = [[MPQ_HASH_ENTRY_EMPTY, MPQ_HASH_ENTRY_EMPTY, MPQ_HASH_ENTRY_EMPTY, MPQ_HASH_ENTRY_EMPTY]] * hash_table_size
    hash_entries = [list(entry) for entry in hash_entries]
    for block_index, (path, offset, size) in enumerate(placements):
        block_entries.append(struct.pack("<4I", offset, size, size, MPQ_FILE_EXISTS | MPQ_FILE_SINGLE_UNIT))
        index = hash_string(path, HASH_TABLE_OFFSET) & (hash_table_size - 1)
        while hash_entries[index][3] != MPQ_HASH_ENTRY_EMPTY:
            index = (index + 1) & (hash_table_size - 1)
        hash_entries[index] = [hash_string(path, HASH_NAME_A), hash_string(path, HASH_NAME_B), 0, block_index]

    # Entry layout: nameA u32, nameB u32, locale u16, platform u16, block index u32.
    # Empty entries keep 0xFF in every byte; used entries are locale/platform 0.
    hash_table = b"".join(
        struct.pack("<IIHHI", e[0], e[1], 0xFFFF if e[3] == MPQ_HASH_ENTRY_EMPTY else 0, 0xFFFF if e[3] == MPQ_HASH_ENTRY_EMPTY else 0, e[3])
        for e in hash_entries
    )
    block_table = b"".join(block_entries)

    hash_table_pos = MPQ_HEADER_SIZE + len(file_data)
    block_table_pos = hash_table_pos + len(hash_table)
    archive_size = block_table_pos + len(block_table)

    header = struct.pack(
        "<4sIIHHIIII",
        MPQ_MAGIC,
        MPQ_HEADER_SIZE,
        archive_size,
        0,          # format version 0
        3,          # sector size shift (unused for single-unit files)
        hash_table_pos,
        block_table_pos,
        hash_table_size,
        len(block_entries),
    )

    with open(out_path, "wb") as out:
        out.write(header)
        out.write(file_data)
        out.write(encrypt_block(hash_table, hash_string("(hash table)", HASH_FILE_KEY)))
        out.write(encrypt_block(block_table, hash_string("(block table)", HASH_FILE_KEY)))


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)

    base_dbc_path, rows_path, out_path = sys.argv[1:4]

    with open(base_dbc_path, "rb") as f:
        base = f.read()
    with open(rows_path, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit("no rows to append")

    patched = append_item_dbc_rows(base, rows)
    write_mpq(out_path, [("DBFilesClient\\Item.dbc", patched)])
    print(f"{out_path}: Item.dbc {len(base)} -> {len(patched)} bytes, {len(rows)} rows appended")


if __name__ == "__main__":
    main()
