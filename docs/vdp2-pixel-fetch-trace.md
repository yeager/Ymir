# VDP2 pixel-fetch trace

Ymir's optional software-renderer trace records why selected normal-scroll
pixels were drawn or made transparent. It is intended for authentic capture
analysis and does not change renderer output. Enabling the trace selects the
software VDP renderer.

The CSV records the zero-based VDP2 frame index and one-based VDP2 frame serial,
field, VCNT row, layer, output X, resolved scroll coordinates, window and
mosaic decisions, pattern-name source address and words, character number and
address, character-data cache/access state, fetched dot value, and final layer
priority. NBG1 is layer 1. A row with `window_blocked=1` has no character or dot
fetch; `mosaic_reused=1` identifies a copied output pixel. `dot_value_valid=0`
means the renderer's cached character bytes do not provide trustworthy dot
data, for example immediately after loading a save state. Pattern-name
provenance is transient and is marked invalid until Ymir performs a new pattern
name fetch.

Tracing is bounded to at most 64 frame indices and 200,000 selected pixels.
Rows and columns use half-open ranges. The CSV has a companion
`.receipt.json`; incomplete writes and cap overflow are reported there.

## Nexus frame 891 example

The retained Ymir layer capture identifies VDP1 frame 891 with VDP2 frame
serials 1279 and 1280. Their zero-based VDP2 frame indices are 1278 and 1279.
Capture both passes in one emulator run to compare the fully transparent pass
with the pass whose NBG1 layer becomes opaque below row 84:

```sh
ymir-sdl3 --debug \
  --vdp2-pixel-fetch-trace /tmp/nexus-frame891-vdp2-pixels.csv \
  --vdp2-pixel-fetch-first-frame 1278 \
  --vdp2-pixel-fetch-end-frame 1280 \
  --vdp2-pixel-fetch-first-row 0 \
  --vdp2-pixel-fetch-end-row 224 \
  --vdp2-pixel-fetch-first-x 0 \
  --vdp2-pixel-fetch-end-x 320 \
  /path/to/authentic-nexus-disc.cue
```

The command captures 143,360 pixel records when both passes cover all 320×224
pixels. Confirm the CSV's `vdp2_frame_serial` for each pass before comparing it
with the retained frame-891 register, VRAM, CRAM, and layer-output artifacts.
A matching PND address does not by itself establish correct character-dot or
window behavior; compare the recorded dot value, access state, window decision,
and final priority as well.
