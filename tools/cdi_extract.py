#!/usr/bin/env python3
"""Dreamcast CDI (DiscJuggler v2/v3/v3.5) Parser and ISO9660 Extractor.

Parses CDI track layout, extracts Dreamcast IP.BIN and ISO9660 files
directly from raw sectors without requiring external emulators or disc mounters.

Usage:
    python tools/cdi_extract.py list game.cdi
    python tools/cdi_extract.py extract game.cdi [--dest build-inputs/extracted]
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path


class DreamcastCDI:
    def __init__(self, cdi_path: Path):
        self.path = Path(cdi_path)
        if not self.path.exists():
            raise FileNotFoundError(f"CDI image not found: {self.path}")

        self.file_size = self.path.stat().st_size
        self._parse_trailer()

    def _parse_trailer(self):
        with open(self.path, "rb") as f:
            f.seek(self.file_size - 8)
            self.version, self.header_offset = struct.unpack("<II", f.read(8))

            f.seek(self.header_offset)
            self.sessions_count = struct.unpack("<H", f.read(2))[0]
            trailer = f.read()

        # Locate all tracks using standard DiscJuggler double start-mark
        mark = b"\x00\x00\x01\x00\x00\x00\xff\xff\xff\xff"
        mark_pair = mark + mark

        self.tracks = []
        cur_file_pos = 0
        search_pos = 0

        while True:
            idx = trailer.find(mark_pair, search_pos)
            if idx == -1:
                break

            p = idx + len(mark_pair)
            p += 4
            name_len = trailer[p]
            p += 1 + name_len + 11 + 4 + 4

            temp = struct.unpack("<I", trailer[p : p + 4])[0]
            p += 4
            if temp == 0x80000000:
                p += 8

            p += 2
            pregap_len, length = struct.unpack("<II", trailer[p : p + 8])
            p += 8 + 6
            mode = struct.unpack("<I", trailer[p : p + 4])[0]
            p += 4 + 12
            start_lba, total_len = struct.unpack("<II", trailer[p : p + 8])
            p += 8 + 16
            sec_val = struct.unpack("<I", trailer[p : p + 4])[0]
            sec_size = [2048, 2336, 2352][sec_val] if sec_val in (0, 1, 2) else 2352

            track_info = {
                "track_num": len(self.tracks) + 1,
                "mode": mode,
                "start_lba": start_lba,
                "sec_size": sec_size,
                "length": length,
                "pregap": pregap_len,
                "total_len": total_len,
                "file_offset": cur_file_pos,
            }
            self.tracks.append(track_info)
            cur_file_pos += total_len * sec_size
            search_pos = p + 20

    def get_data_track(self):
        """Locate the primary ISO9660 data track (typically session 2)."""
        data_tracks = [t for t in self.tracks if t["mode"] in (1, 2)]
        if not data_tracks:
            raise ValueError("No data track found in CDI image.")
        return data_tracks[-1]

    def read_sector_data(self, track: dict, rel_sector: int) -> bytes:
        """Read 2048 bytes of user data from a given sector relative to track start."""
        with open(self.path, "rb") as f:
            abs_sec = track["pregap"] + rel_sector
            f.seek(track["file_offset"] + abs_sec * track["sec_size"])
            raw = f.read(track["sec_size"])

            if track["sec_size"] == 2048:
                return raw
            elif track["sec_size"] == 2336:
                return raw[8:2056]
            elif track["sec_size"] == 2352:
                header_offset = 16 if track["mode"] == 1 else 24
                return raw[header_offset : header_offset + 2048]
            return raw[:2048]

    def read_track_bytes(self, track: dict, rel_lba: int, total_size: int) -> bytes:
        data = bytearray()
        sec_count = (total_size + 2047) // 2048
        for s in range(sec_count):
            chunk = self.read_sector_data(track, rel_lba + s)
            data.extend(chunk)
        return bytes(data[:total_size])

    def get_ip_bin(self) -> bytes:
        """Read the 32KB boot sector (IP.BIN) from sector 0 of data track."""
        track = self.get_data_track()
        return self.read_track_bytes(track, 0, 32768)

    def list_files(self):
        """Parse ISO9660 filesystem in data track."""
        track = self.get_data_track()
        pvd = self.read_sector_data(track, 16)
        if pvd[1:6] != b"CD001":
            raise ValueError("Invalid ISO9660 PVD in Dreamcast data track.")

        root_record = pvd[156 : 156 + 34]
        root_lba = int.from_bytes(root_record[2:6], "little")
        root_size = int.from_bytes(root_record[10:14], "little")

        rel_root_lba = root_lba - track["start_lba"] if root_lba >= track["start_lba"] else root_lba

        files = []
        self._scan_dir(track, rel_root_lba, root_size, "", files)
        return files

    def _scan_dir(self, track: dict, rel_lba: int, size: int, parent_path: str, files_list: list):
        dir_data = self.read_track_bytes(track, rel_lba, size)
        pos = 0
        while pos < len(dir_data):
            rec_len = dir_data[pos]
            if rec_len == 0:
                next_boundary = ((pos // 2048) + 1) * 2048
                if next_boundary <= pos or next_boundary >= len(dir_data):
                    break
                pos = next_boundary
                continue

            rec = dir_data[pos : pos + rec_len]
            abs_lba = int.from_bytes(rec[2:6], "little")
            file_size = int.from_bytes(rec[10:14], "little")
            flags = rec[25]
            name_len = rec[32]
            raw_name = rec[33 : 33 + name_len].decode("latin1", errors="replace")

            clean_name = raw_name.split(";")[0]
            if clean_name in ("\x00", "\x01", ""):
                pos += rec_len
                continue

            rel_file_lba = (
                abs_lba - track["start_lba"] if abs_lba >= track["start_lba"] else abs_lba
            )
            rel_path = f"{parent_path}/{clean_name}" if parent_path else clean_name
            is_dir = bool(flags & 0x02)

            files_list.append(
                {
                    "path": rel_path,
                    "name": clean_name,
                    "raw_name": raw_name,
                    "abs_lba": abs_lba,
                    "rel_lba": rel_file_lba,
                    "size": file_size,
                    "is_dir": is_dir,
                }
            )

            if is_dir:
                self._scan_dir(track, rel_file_lba, file_size, rel_path, files_list)

            pos += rec_len


def main():
    parser = argparse.ArgumentParser(description="Dreamcast CDI ISO9660 Extractor")
    parser.add_argument("command", choices=["list", "extract"], help="Action to perform")
    parser.add_argument("cdi", type=Path, help="Path to .cdi file")
    parser.add_argument("--dest", default="build-inputs/extracted", help="Extraction destination directory")
    args = parser.parse_args()

    cdi = DreamcastCDI(args.cdi)
    print(f"Loaded CDI: {cdi.path} (Ver: 0x{cdi.version:08x}, Sessions: {cdi.sessions_count})")
    for t in cdi.tracks:
        sess = t.get("session", t["track_num"])
        print(
            f"  Track {t['track_num']} (Sess {sess}): Mode={t['mode']}, LBA={t['start_lba']}, SecSize={t['sec_size']}, TotalSec={t['total_len']:,}"
        )

    files = cdi.list_files()

    if args.command == "list":
        print(f"\nDiscovered {len(files)} files in Dreamcast ISO filesystem:\n")
        print(f"{'Type':<5} {'LBA':<8} {'Size (bytes)':<14} {'Path'}")
        print("-" * 65)
        for f in files:
            ftype = "DIR" if f["is_dir"] else "FILE"
            print(f"{ftype:<5} {f['rel_lba']:<8} {f['size']:<14,d} {f['path']}")

    elif args.command == "extract":
        dest_root = Path(args.dest)
        track = cdi.get_data_track()
        print(f"\nExtracting Dreamcast filesystem to {dest_root.resolve()}...")
        count = 0

        # Save IP.BIN
        ip_bin = cdi.get_ip_bin()
        ip_path = dest_root / "IP.BIN"
        ip_path.parent.mkdir(parents=True, exist_ok=True)
        ip_path.write_bytes(ip_bin)
        print(f"-> Extracted IP.BIN ({len(ip_bin):,} bytes) [SHA-1: {hashlib.sha1(ip_bin).hexdigest()}]")

        for f in files:
            if not f["is_dir"]:
                out_path = dest_root / "disc_fs" / f["path"]
                out_path.parent.mkdir(parents=True, exist_ok=True)
                content = cdi.read_track_bytes(track, f["rel_lba"], f["size"])
                out_path.write_bytes(content)
                count += 1

                if f["name"].upper() == "1ST_READ.BIN":
                    target_bin = dest_root / "1ST_READ.BIN"
                    target_bin.write_bytes(content)
                    sha1 = hashlib.sha1(content).hexdigest()
                    print(f"-> Extracted 1ST_READ.BIN ({len(content):,} bytes) [SHA-1: {sha1}]")

        print(f"Extracted {count} files successfully.")


if __name__ == "__main__":
    main()
