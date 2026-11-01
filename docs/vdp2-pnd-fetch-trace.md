# VDP2 PND fetch trace

Ymir can emit a bounded CSV of pattern-name data (PND) reads performed by its software VDP2 renderer for visible NBG1 and NBG2 normal-scroll pixels. This is an opt-in diagnostic for comparing real game VRAM fetches with capture data; it does not alter rendering.

    ymir-sdl3 --disc game.cue --pnd-fetch-trace /tmp/game-pnd.csv --pnd-fetch-first-frame 0 --pnd-fetch-end-frame 8 --pnd-fetch-first-row 0 --pnd-fetch-end-row 263

Frame and row end values are exclusive. The default range is frames [0,64) and VCNT rows [0,512). The maximum frame range is 64, the maximum row value is 2048, and the CSV is capped at 1,000,000 rows. Only NBG1 and NBG2 are recorded. Tracing selects the software renderer automatically.

The CSV starts with # ymir-vdp2-pnd-fetch-csv-v1, followed by a header and one row for each actual PND read. Fields include frame ordinal, alternate field, VCNT row, NBG layer, screen X, scroll X/Y, byte address, PND width, and raw PND value. screen_x is the visible pixel whose fetch caused the PND read; repeated pixels that reuse a cached character do not create duplicate records. The speculative fetch after the visible end of a scanline is excluded.

A sibling .receipt.json reports the requested ranges, row limit, rows written and dropped, write errors, and observed frame/row counts. complete means no matching fetch was dropped and no CSV write failed. Observed counts describe rows present in the trace and do not claim that every requested row or frame produced a PND fetch. Keep the CSV and receipt together when using them as evidence.
