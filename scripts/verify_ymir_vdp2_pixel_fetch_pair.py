#!/usr/bin/env python3
"""Validate pixel-fetch samples against adjacent authentic VDP2 VRAM snapshots."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


class ValidationError(Exception):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_be32(data: bytes, address: int) -> int:
    return int.from_bytes(data[address & 0x7FFFF : (address & 0x7FFFF) + 4], "big")


def fetch_4bpp_dot(data: bytes, sample: dict[str, str]) -> tuple[int, int]:
    pattern = int(sample["raw_pnd"], 16)
    dot_x = int(sample["scroll_x"]) & 7
    dot_y = int(sample["scroll_y"]) & 7
    if pattern & 0x40000000:
        dot_x ^= 7
    if pattern & 0x80000000:
        dot_y ^= 7
    address = (int(sample["char_address"], 16) + dot_y * 4 + dot_x // 2) & 0x7FFFF
    packed = data[address]
    value = packed >> 4 if dot_x % 2 == 0 else packed & 0x0F
    return address, value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("previous_vram", type=Path, help="VRAM captured from the preceding VDP2 pass")
    parser.add_argument("current_vram", type=Path, help="VRAM captured from the selected VDP2 pass")
    parser.add_argument("--frame", type=int, required=True, help="zero-based VDP2 frame index")
    parser.add_argument("--serial", type=int, required=True, help="one-based VDP2 frame serial")
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--layer", type=int, default=1)
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args()

    try:
        receipt = json.loads(args.receipt.read_text(encoding="utf-8"))
        if receipt.get("format") != "ymir-vdp2-pixel-fetch-receipt-v1" or receipt.get("complete") is not True:
            raise ValidationError("trace receipt is missing or incomplete")
        if receipt.get("rows_dropped") != 0 or receipt.get("write_errors") != 0:
            raise ValidationError("trace receipt reports dropped rows or write errors")

        previous = args.previous_vram.read_bytes()
        current = args.current_vram.read_bytes()
        if len(previous) != 0x80000 or len(current) != 0x80000:
            raise ValidationError("VDP2 VRAM snapshots must each be exactly 512 KiB")

        required_columns = {
            "frame", "vdp2_frame_serial", "field", "row", "layer", "screen_x",
            "pnd_source_valid", "pnd_access_allowed", "pnd_address", "pnd_width_bytes",
            "raw_pnd", "char_num", "char_address", "char_data_valid", "scroll_x", "scroll_y",
            "dot_address", "dot_value", "dot_value_valid", "priority",
        }
        selected: dict[tuple[int, int], dict[str, str]] = {}
        total_rows = 0
        with args.trace.open(newline="", encoding="utf-8") as stream:
            if stream.readline().rstrip("\n") != "# ymir-vdp2-pixel-fetch-csv-v1":
                raise ValidationError("unexpected pixel trace format")
            rows = csv.DictReader(stream)
            if not required_columns.issubset(rows.fieldnames or []):
                raise ValidationError("pixel trace is missing required source columns")
            for row in rows:
                total_rows += 1
                if (int(row["frame"]) != args.frame or int(row["vdp2_frame_serial"]) != args.serial
                        or int(row["layer"]) != args.layer or int(row["field"]) != 0):
                    continue
                key = (int(row["row"]), int(row["screen_x"]))
                if key in selected:
                    raise ValidationError(f"duplicate pixel sample {key}")
                selected[key] = row

        if total_rows != receipt.get("rows_written"):
            raise ValidationError("CSV row count does not match the trace receipt")
        expected = {(y, x) for y in range(args.height) for x in range(args.width)}
        if selected.keys() != expected:
            raise ValidationError(f"coverage mismatch: {len(expected - selected.keys())} missing, "
                                  f"{len(selected.keys() - expected)} out of range")

        source_counts: collections.Counter[str] = collections.Counter()
        dot_match_counts: collections.Counter[str] = collections.Counter()
        row_sources: dict[int, collections.Counter[str]] = collections.defaultdict(collections.Counter)
        nonzero_priority = 0
        for (y, _x), sample in selected.items():
            pnd_address = int(sample["pnd_address"], 16)
            raw_pnd = int(sample["raw_pnd"], 16)
            matches = []
            if sample["pnd_source_valid"] == "1" and sample["pnd_access_allowed"] == "1":
                if read_be32(previous, pnd_address) == raw_pnd:
                    matches.append("previous")
                if read_be32(current, pnd_address) == raw_pnd:
                    matches.append("current")
            source = "+".join(matches) if matches else "neither"
            source_counts[source] += 1
            row_sources[y][source] += 1

            dot_ok = False
            if sample["char_data_valid"] == "1" and sample["dot_value_valid"] == "1":
                dot_address = int(sample["dot_address"], 16)
                dot_value = int(sample["dot_value"])
                dot_ok = any(fetch_4bpp_dot(vram, sample) == (dot_address, dot_value)
                             for vram, name in ((previous, "previous"), (current, "current"))
                             if name in matches)
            dot_match_counts["matched" if dot_ok else "mismatched"] += 1
            nonzero_priority += int(sample["priority"]) != 0

        rows_with_source = {
            str(y): dict(sorted(counts.items()))
            for y, counts in sorted(row_sources.items())
        }
        result = {
            "trace_sha256": sha256(args.trace),
            "receipt_sha256": sha256(args.receipt),
            "previous_vram_sha256": sha256(args.previous_vram),
            "current_vram_sha256": sha256(args.current_vram),
            "frame": args.frame,
            "vdp2_frame_serial": args.serial,
            "layer": args.layer,
            "dimensions": [args.width, args.height],
            "pixels": len(selected),
            "pnd_source_matches": dict(sorted(source_counts.items())),
            "dot_matches_source_vram": dict(sorted(dot_match_counts.items())),
            "nonzero_priority_pixels": nonzero_priority,
            "row_source_matches": rows_with_source,
            "interpretation": "sample provenance validated against adjacent pass snapshots; not a final composition result",
        }
        if source_counts.get("neither", 0) or dot_match_counts.get("mismatched", 0):
            raise ValidationError(json.dumps(result, indent=2))
        encoded = json.dumps(result, indent=2) + "\n"
        if args.output_json:
            args.output_json.write_text(encoded, encoding="utf-8")
        print(encoded, end="")
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError, ValidationError) as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
