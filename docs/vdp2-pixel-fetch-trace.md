# VDP2 pixel-fetch trace

Ymir's optional software-renderer trace records why selected normal-scroll
pixels were drawn or made transparent. It is intended for authentic capture
analysis and does not change renderer output. Enabling the trace selects the
software VDP renderer.

The CSV records the zero-based VDP2 frame index and one-based VDP2 frame serial,
field, VCNT row, layer, output X, resolved scroll coordinates, window and
mosaic decisions, pattern-name source address and words, character number and
address, character-data cache/access state, fetched dot value, and final layer
priority. NBG1 is layer 1. Raw `dot_value` is populated for 16-color and
256-color palette pixels (4 bpp and 8 bpp). Other color formats leave that
field at its default and must not be interpreted as a fetched dot. A row with
`window_blocked=1` has no character or dot fetch; `mosaic_reused=1` identifies
a copied output pixel.
dot_value_valid=0 means the renderer's cached character bytes do not provide
trustworthy dot data, for example immediately after loading a save state.
Pattern-name provenance is transient and is marked invalid until Ymir performs
a new pattern name fetch.

Tracing is bounded to at most 64 frame indices and 200,000 selected pixels.
Rows and columns use half-open ranges. The CSV has a companion
.receipt.json; incomplete writes and cap overflow are reported there.

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
pixels. Confirm the CSV's vdp2_frame_serial for each pass before comparing it
with the retained frame-891 register, VRAM, CRAM, and layer-output artifacts.
A matching PND address does not by itself establish correct character-dot or
window behavior; compare the recorded dot value, access state, window decision,
and final priority as well.

When tracing after loading a save state, use the deterministic frame-capture
route. Loading a state reconstructs the VDP and clears renderer-owned tracer
hooks. The CLI capture reattaches the pixel tracer immediately before its first
scheduled frame, so the trace covers the requested replay rather than only the
startup frames. For the retained MAPD frame-891 save state, slot 3 and a
1,600-frame replay can be captured with:

```sh
ymir-sdl3 --debug --load-state 3 --capture-frames 1600 \
  --capture-start-frame 0 --capture-start-hold 60 --dump-memory \
  --vdp2-pixel-fetch-trace /tmp/nexus-mapd-frame891.csv \
  --vdp2-pixel-fetch-first-frame 1278 \
  --vdp2-pixel-fetch-end-frame 1280 \
  /path/to/authentic-nexus-disc.cue
```

This captures the two zero-based passes corresponding to VDP2 frame serials
1279 and 1280. The save slot must belong to the selected profile and match the
disc, IPL, and CD block ROM hashes. Keep the resulting trace receipt and memory
dump with the run; frame numbers alone do not prove the replay reached the
intended game state.

## Initial authentic save-state runtime check

The trace was also exercised against an authentic Nexus gameplay save state
from the external Firestaff evidence corpus. The state SHA-256 is
`156fa6982798ac7850ff21d12b7ce0b7dee278e0f501f55e18178fd96a2dce2e`; its
Japanese retail disc and IPL hashes match the pinned Nexus inputs above. The
loaded state has `BGON=0x0007`, `CHCTLA=0x1013`, and `CHCTLB=0x0002`. Ymir's
deterministic frame-step route produced two complete 320×224 NBG1 passes:
143,360 pixel records, zero dropped rows, and zero write errors. Every pixel
has a valid PND source; 74 pixels per pass have nonzero layer priority.

The built executable is installed on TRV2 as
`/usr/local/bin/ymir-sdl3-vdp2-trace`.

A paired authentic state capture confirms the register tuple and raw VDP2
memory. The dominant PND `0x20001100` selects address `0x22000`, whose 32-byte
span is zero-filled in the dump. Three sparse PND references select `0x22200`,
`0x22580`, and `0x228c0`, with 2, 9, and 10 nonzero bytes in their respective
32-byte spans. The provenance addresses therefore resolve into the captured
VRAM, but the pixel trace still reports those data/dot fetches as invalid. The
raw memory dump does not turn those invalid per-pixel samples into dot evidence.

This immediate post-load check does not establish character-dot parity:
`char_data_valid` and `dot_value_valid` are zero for all 71,680 pixels in both
passes. A separate 56-frame probe also found no valid character data in the
sampled x-range. The retained trace and receipts are under
`/Volumes/Extern-disk/firestaff-evidence/nexus/ymir-vdp2-pixel-fetch-trace-20261101/active-layer/`.
This initial trace predates the 256-color dot-fetch instrumentation. It verifies
bounded tracing and pattern-name provenance on real game data, but its dot
values remain unknown and must not be used as NBG1 pixel-output evidence.

## Verified 256-color gameplay composition

After adding Palette256 fetch fields, a deterministic replay of the same
authentic Japanese retail gameplay state produced a complete 320×224 NBG1
trace. All 71,680 rows have valid PND, character-data, and dot fields. The dot
histogram is 71,606 zero-valued dots and 74 dots with value 3. An independent
join against the paired raw VDP2 VRAM dump found zero dot-byte mismatches; all
74 nonzero dots have priority 5 and all zero dots have priority 0.

For this same VDP2 pass, Firestaff's capture-only priority compositor produced
64,804 VDP1 pixels, 3,297 NBG0 pixels, 74 NBG1 pixels, and 3,505 NBG2 pixels.
Its RGB output matched Ymir's final composed 320×224 RGBX framebuffer at all
71,680 pixels. The run and its exact source hashes, trace, receipts, VDP memory
snapshots, and comparison output are retained in the external evidence
directory `firestaff-evidence/nexus/ymir-vdp2-gameplay-pass0-20261101/`.

This verifies one authentic save-state frame and the capture-only compositor
for that state. It does not establish hardware parity, parity in other scenes
or video modes, or readiness to use the compositor as the live renderer.
