#!/usr/bin/env python3
"""Validate the versioned, bounded Ymir VDP2 PND fetch CSV and receipt."""
import argparse
import csv
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("csv", type=Path)
args = parser.parse_args()
receipt_path = Path(str(args.csv) + ".receipt.json")
receipt = json.loads(receipt_path.read_text())
with args.csv.open(newline="") as stream:
    version = stream.readline().strip()
    if version != "# ymir-vdp2-pnd-fetch-csv-v1":
        raise SystemExit(f"unsupported trace version: {version}")
    rows = list(csv.DictReader(stream))
if len(rows) != receipt["rows_written"]:
    raise SystemExit("CSV row count does not match receipt")
if len(rows) > receipt["row_limit"] or receipt["rows_dropped"] != 0 or receipt["write_errors"] != 0:
    raise SystemExit("trace exceeded its bounds or had write errors")
for row in rows:
    if int(row["layer"]) not in (1, 2):
        raise SystemExit("unexpected layer")
    if int(row["pnd_width_bytes"]) not in (2, 4):
        raise SystemExit("invalid PND width")
    if not (0 <= int(row["pnd_address"], 16) < 0x80000):
        raise SystemExit("PND address is outside VDP2 VRAM")
print(f"valid rows={len(rows)} frames={receipt['frames_observed']} rows_observed={receipt['rows_observed']} complete={receipt['complete']}")
