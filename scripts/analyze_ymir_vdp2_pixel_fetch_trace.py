#!/usr/bin/env python3
"""Validate and summarize an authentic Ymir VDP2 pixel-fetch trace."""

from __future__ import annotations

import argparse
import collections
import csv
import hashlib
import json
from pathlib import Path


class TraceError(Exception):
    pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--frame", type=int, required=True, help="zero-based VDP2 frame index")
    parser.add_argument("--serial", type=int, required=True, help="one-based VDP2 frame serial")
    parser.add_argument("--layer", type=int, choices=(1, 2), default=1)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--output-json", type=Path)
    args = parser.parse_args()

    try:
        receipt = json.loads(args.receipt.read_text(encoding="utf-8"))
        if receipt.get("format") != "ymir-vdp2-pixel-fetch-receipt-v1":
            raise TraceError("unexpected receipt format")
        if receipt.get("complete") is not True or receipt.get("rows_dropped") != 0 or receipt.get("write_errors") != 0:
            raise TraceError("trace receipt is incomplete, dropped rows, or has write errors")

        with args.trace.open(newline="", encoding="utf-8") as stream:
            header = stream.readline().rstrip("\n")
            if header != "# ymir-vdp2-pixel-fetch-csv-v1":
                raise TraceError("unexpected trace format marker")
            rows = csv.DictReader(stream)
            expected = {
                "frame", "vdp2_frame_serial", "field", "row", "layer", "screen_x", "scroll_x", "scroll_y",
                "window_blocked", "mosaic_reused", "mosaic_source_x", "pnd_source_valid", "pnd_access_allowed",
                "pnd_address", "pnd_width_bytes", "raw_pnd", "char_num", "char_address", "char_data_cache_hit",
                "char_pattern_access_allowed", "char_data_valid", "dot_address", "dot_value", "dot_value_valid",
                "priority",
            }
            if set(rows.fieldnames or []) != expected:
                raise TraceError("pixel trace CSV schema does not match v1")

            selected: dict[tuple[int, int, int], dict[str, str]] = {}
            total_rows = 0
            for row in rows:
                total_rows += 1
                if int(row["frame"]) != args.frame or int(row["vdp2_frame_serial"]) != args.serial:
                    continue
                if int(row["layer"]) != args.layer or int(row["field"]) != 0:
                    continue
                key = (int(row["row"]), int(row["screen_x"]), int(row["layer"]))
                if key in selected:
                    raise TraceError(f"duplicate pixel sample {key}")
                selected[key] = row
            if total_rows != receipt.get("rows_written"):
                raise TraceError("CSV row count does not match the receipt")

        expected_pixels = {(y, x, args.layer) for y in range(args.height) for x in range(args.width)}
        missing = expected_pixels - selected.keys()
        extra = selected.keys() - expected_pixels
        if missing or extra:
            raise TraceError(f"coverage mismatch: {len(missing)} missing, {len(extra)} out of range")

        records = list(selected.values())
        dots = collections.Counter(int(row["dot_value"]) for row in records if row["dot_value_valid"] == "1")
        rows_by_y: dict[int, list[dict[str, str]]] = collections.defaultdict(list)
        for (row_y, _, _), row in selected.items():
            rows_by_y[row_y].append(row)
        row_summaries = {}
        for y in range(args.height):
            line = rows_by_y[y]
            row_summaries[str(y)] = {
                "nonzero_priority_pixels": sum(row["priority"] != "0" for row in line),
                "window_blocked_pixels": sum(row["window_blocked"] == "1" for row in line),
                "zero_dot_pixels": sum(row["dot_value_valid"] == "1" and row["dot_value"] == "0" for row in line),
                "invalid_dot_pixels": sum(row["dot_value_valid"] != "1" for row in line),
            }
        result = {
            "trace_sha256": hashlib.sha256(args.trace.read_bytes()).hexdigest(),
            "receipt_sha256": hashlib.sha256(args.receipt.read_bytes()).hexdigest(),
            "frame": args.frame,
            "vdp2_frame_serial": args.serial,
            "layer": args.layer,
            "dimensions": [args.width, args.height],
            "pixels": len(records),
            "nonzero_priority_pixels": sum(row["priority"] != "0" for row in records),
            "window_blocked_pixels": sum(row["window_blocked"] == "1" for row in records),
            "mosaic_reused_pixels": sum(row["mosaic_reused"] == "1" for row in records),
            "pnd_source_invalid_pixels": sum(row["pnd_source_valid"] != "1" for row in records),
            "pnd_access_denied_pixels": sum(row["pnd_source_valid"] == "1" and row["pnd_access_allowed"] != "1"
                                             for row in records),
            "char_data_invalid_pixels": sum(row["char_data_valid"] != "1" for row in records),
            "char_access_denied_pixels": sum(row["char_data_valid"] == "1" and
                                              row["char_pattern_access_allowed"] != "1" for row in records),
            "dot_value_invalid_pixels": sum(row["dot_value_valid"] != "1" for row in records),
            "dot_value_histogram": {str(value): count for value, count in sorted(dots.items())},
            "row_summary": row_summaries,
            "interpretation": "renderer fetch provenance for one exact VDP2 pass; this is diagnostic evidence, not a composition result",
        }
        encoded = json.dumps(result, indent=2) + "\n"
        if args.output_json:
            args.output_json.write_text(encoded, encoding="utf-8")
        print(encoded, end="")
        return 0
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError, TraceError) as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
