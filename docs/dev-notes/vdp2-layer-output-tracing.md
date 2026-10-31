# VDP2 layer output tracing

`--trace-vdp2-layer-outputs` records the software renderer's per-layer scanline
buffers immediately before `VDP2ComposeLine`. It complements
`--trace-vdp2-rendered-frames`: the latter records the composed result, while
this trace exposes each layer's color, priority, and special color-calculation
flag before composition.

The option is opt-in and bounded to 64 MiB. It requires the existing
`--trace-vdp2-render-state` capture, both render-state range options, a
deterministic rendered capture, `--debug`, and `--dump-memory`. The selected
frame and row ranges are shared by all three outputs. Each record carries the
VDP1 frame index, VDP2 frame serial, line-call ordinal, row, field, layer ID,
and width. The VDP2 serial and line-call ordinal distinguish repeated scanline
passes within one VDP1 frame.

Example for a single VDP1 frame and all visible rows:

```sh
ymir-sdl3 --load-state 3 --capture-frames 900 --capture-no-present \
  --dump-memory --debug \
  --trace-vdp2-render-state /capture/render-state.csv \
  --trace-vdp2-rendered-frames /capture/rendered-frames \
  --trace-vdp2-layer-outputs /capture/layer-outputs.bin \
  --trace-vdp2-render-state-frame-range 891:892 \
  --trace-vdp2-render-state-row-range 0:224
```

The binary starts with the eight-byte magic `YMLYR001`. It is followed by
fixed-width little-endian record headers and three pixel planes: 32-bit color
values, 8-bit priorities, and 8-bit special-color-calculation flags. The
adjacent `.receipt.txt` documents the exact record layout, layer mapping,
record count, byte count, and any output limit or write error.

Layer IDs are `0` sprite, `1` RBG0, `2` NBG0 or RBG1, `3` NBG1 or EXBG, `4`
NBG2, `5` NBG3, and `6` transparent mesh. The shared slots reflect Ymir's
rotation-background mode. These values describe Ymir's software-renderer input
buffers; they do not themselves claim hardware or Firestaff parity.
