# Ymir

[![Stable release](https://github.com/ymir-emu/Ymir/actions/workflows/stable-release.yaml/badge.svg)](https://github.com/ymir-emu/Ymir/actions/workflows/stable-release.yaml) [![Nightly release](https://github.com/ymir-emu/Ymir/actions/workflows/nightly-release.yaml/badge.svg)](https://github.com/ymir-emu/Ymir/actions/workflows/nightly-release.yaml) <a href="https://discord.gg/NN3A7n5dzn">![Discord Shield](https://discord.com/api/guilds/1368676375627694341/widget.png?style=shield)</a> <a href="https://patreon.com/StrikerX3">![Patreon Shield](https://img.shields.io/badge/Patreon-F96854?style=flat&logo=patreon&logoColor=white)</a>

A work-in-progress Sega Saturn emulator.

Join the [Discord community](https://discord.gg/NN3A7n5dzn).

Find the official compatibility list [here](https://docs.google.com/spreadsheets/d/1SLZzL9LelSlpEmTKy8cjaQnE7mew2uW1rfCgcekO58Q/edit?usp=sharing).

Grab the latest release: [stable](https://github.com/ymir-emu/Ymir/releases/latest), [nightly](https://github.com/ymir-emu/Ymir/releases/latest-nightly).

> [!IMPORTANT]
> Windows users: install the latest [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vc14/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vc14/vc_redist.arm64.exe)) before running the emulator.
> This is **mandatory** to avoid crashes on startup.
>
> macOS users: follow [these instructions](https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unknown-developer-mh40616/mac) to allow Ymir to run on your system. Ymir is signed with an ad-hoc certificate, flagging it as unverified.
>
> Linux users: if you installed the Flatpak version, make sure to [grant it permission to access the filesystem](TROUBLESHOOTING.md#game-discs-dont-load-with-the-flatpak-release) or your disc images won't load properly.

<div class="grid" markdown>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/cd-player.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/sonic-r.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/virtua-fighter-2.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/radiant-silvergun.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/panzer-dragoon-saga.png"/>
  <img width="49.5%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/nights-into-dreams.png"/>
  <img width="100%" src="https://github.com/ymir-emu/Ymir/blob/main/docs/images/debugger.png"/>
</div>


## Features

- Load games from MAME CHD, BIN+CUE, IMG+CCD, MDF+MDS or ISO files
- Automatic IPL (BIOS) ROM detection
- Automatic region switching
- Up to two players with a variety of controllers on both ports
- Fully customizable keybindings
- Backup RAM, DRAM and ROM cartridges (more to come)
- Integrated backup memory manager to import and export saves, and transfer between internal and cartridge RAM
- Forwards-compatible save states
- Rewinding (up to one minute at 60 fps), turbo speed, frame step (forwards and backwards)
- Full screen mode with VRR support and low input lag
- Graphics enhancements such as optional deinterlaced/progressive rendering of high resolution modes and transparent mesh polygon rendering
- Optional low level CD block emulation
- A work-in-progress feature-rich debugger

Ymir runs on Windows 10 or later, macOS 12 (Monterey) or later, most modern and popular Linux distributions and FreeBSD, and supports x86-64 (Intel, AMD) and ARM CPUs.


## Usage

Grab the latest release [here](https://github.com/ymir-emu/Ymir/releases/latest).
Check the [Releases](https://github.com/ymir-emu/Ymir/releases) page for previous versions.

Ymir does not require installation. Simply download it to any directory and run the executable.
On Windows you also need to install the latest [Microsoft Visual C++ Redistributable package](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) ([x86_64 installer](https://aka.ms/vc14/vc_redist.x64.exe), [AArch64/ARM64 installer](https://aka.ms/vc14/vc_redist.arm64.exe)).
It is recommended to install the latest GPU drivers in order to use Direct3D or Vulkan:
- Nvidia: [Find your GPU here](https://www.nvidia.com/Download/index.aspx?lang=en-us) or [use the Nvidia App to update drivers automatically](https://www.nvidia.com/en-us/software/nvidia-app/)
- AMD: [Find your GPU or CPU here](https://www.amd.com/en/support) or use the download buttons at the top to update drivers automatically
- Intel: [Find your GPU or CPU here](https://www.intel.com/content/www/us/en/download-center/home.html)

The program accepts command-line arguments. Invoke `ymir-sdl3 --help` to list the options:

```
Ymir - Sega Saturn emulator
Usage:
  Ymir [OPTION...] path to disc image

  -d, --disc arg      Path to Saturn disc image (.ccd, .chd, .cue, .iso, 
                      .mds)
  -p, --profile arg   Path to profile directory
  -u, --user          Force user profile
  -h, --help          Display help text
  -f, --fullscreen    Start in fullscreen mode
  -P, --paused        Start paused
  -F, --fast-forward  Start in fast-forward mode
  -D, --debug         Start with debug tracing enabled
  -E, --exceptions    Capture all unhandled exceptions

```

The options are case-sensitive -- lowercase `-p` sets the profile path, uppercase `-P` makes the emulator start paused.

Pass a path to a valid Saturn disc image as an argument to `ymir-sdl3` to launch the emulator with the disc. `-d`/`--disc` is optional.

Use `-p <profile-path>` to point to a separate set of configuration and state files, useful if you wish to have different user profiles (hence the name).

The `-u` option forces usage of the OS's user profile folder (e.g. `C:\Users\<username>\AppData\Roaming\StrikerX3\Ymir` on Windows or `/home/<username>/.local/share/Ymir` on Linux).

For VDP2 graphics reverse engineering, start with `--debug`, open **Debug > VDP > Memory write trace**, select VRAM and/or CRAM, and enable capture. The address interval is start-inclusive and end-exclusive. **Export synchronized bundle** writes the bounded trace, VDP2 VRAM, CRAM, registers, and a receipt into the profile's `dumps` directory from one emulator-thread callback. Each row records the VDP1 frame index, mapped VDP2 destination address, write width, and value. A 32-bit bus store that is split into two 16-bit accesses appears as two rows. Writes before the first VDP1 frame have an `unknown` frame index. Resetting the VDP clears the buffered trace so frame indices from before and after reset are not mixed. The address filter includes writes that overlap the selected interval. The capture retains the newest 250,000 matching writes and reports how many older entries were discarded. The deterministic memory-dump receipt records completion for the trace, VRAM, CRAM, and register files; it marks the bundle complete only when all four writes succeed. The trace does not identify the SH-2 or DMA source of a write.

### Per-line NBG0 render-state trace

Use `--trace-vdp2-render-state CSV` to record the effective NBG0 parameters at
software-renderer line entry, after per-line preparation and before NBG0 drawing.
The CSV tags every sample with the zero-based VDP1 `frame_index` and vertical
`row_vcnt`; it also records scroll, increments, fractional scroll, priority,
color-calculation state, bitmap/color format, and layer-enable state. Scroll and
increment values use 11.8 fixed-point units. The frame index is the VDP1 counter,
not the capture-loop frame used by scheduled input.

The CSV option requires all of the following: `--debug`, either
`--capture-frames` or `--capture-boot-frames`, `--dump-memory`, and the software
renderer. It captures rendered lines; `--capture-no-present` is compatible,
but `--capture-no-render` is not. Supply both range options:

- `--trace-vdp2-render-state-frame-range BEGIN:END` selects a nonempty,
  half-open interval of VDP1 frame indices.
- `--trace-vdp2-render-state-row-range BEGIN:END` selects a nonempty,
  half-open vertical-counter interval. The bounds must be within 0 through 1024.

The selected frame-row product is limited to 500,000 pairs. Each CSV row has
explicit VDP1 frame, VDP2 frame serial, line-call ordinal, row, and
alternate-field tags so it can be joined to other captures without inferring
coordinates. The VDP2 serial increments at each software-renderer frame begin;
the line-call ordinal increments for each `VDP2RenderLine` call within that
serial. Interlaced alternate-field samples share the line-call ordinal and are
tagged separately from the main-field coverage.

VDP1 frame indices and VDP2 frame serials describe different boundaries. A
single VDP1 frame index can contain multiple VDP2 frame serials. For example,
an authentic Japanese Nexus capture at VDP1 frame 7716 recorded rows 100 through
111 once for VDP2 serial 8476 and again for serial 8477; both passes had
`alternate_field=0` and line-call ordinals matching their row values. This is
two distinct VDP2 render passes with one VDP1 frame label, not duplicate line
callbacks within one VDP2 frame. Use both frame fields when comparing samples
from this capture path.

Ymir writes `CSV.receipt.txt` beside the CSV. It records the requested bounds,
expected and observed main samples, unique frame-row coverage, missing and
duplicate counts, unique VDP2 frame-row pairs, observed VDP2 frame serials,
alternate-field samples, and output errors. Its legacy `complete` status
requires one main sample per requested VDP1 frame-row pair, so multiple VDP2
passes under one VDP1 label remain `incomplete` even when each pass has a
complete set of line calls. Inspect the serial and ordinal fields to identify
the pass structure; do not silently deduplicate the samples. The receipt says
`capture_status=complete` only when every expected frame-row pair was captured
exactly once and CSV/receipt writes succeeded. A missing or zero-sample capture
is marked `incomplete`, even if the CSV header was written; check the receipt
before using the trace. Ymir opens and checks the CSV header before starting the
capture, and reports an error with a nonzero exit status if the output path
cannot be opened.

A full-row follow-up for the same Nexus VDP1 frame requested rows 0–223 and
observed 448 main-field samples: serials 8476 and 8477 each contained all 224
line-call ordinals once. The receipt remained incomplete under the VDP1
frame-row contract (448 samples for 224 expected pairs). This verifies two
complete render-state line passes, but does not retain separate pixel images
in the original run. Use the frame-output option below to retain each pass.
The raw row capture and receipt are preserved in the external
Firestaff evidence package `firestaff-evidence/nexus/ymir_nbg0_frame_serial_diagnostic_20261030/`.

Add `--trace-vdp2-rendered-frames DIR` alongside the render-state CSV to save
the composed software-renderer framebuffer at each completed VDP2 pass whose
VDP1 frame index is in the selected range. The VDP2 line renderer includes the
VDP1 sprite plane and enabled VDP2 layers before it invokes the frontend frame
callback. These files can be compared with a same-frame frontend screenshot. The directory must be new or
empty. `index.csv` records VDP1 frame, VDP2 serial, dimensions, format and file
for each output. Pixel files preserve the callback buffer bytes in RGBX order (the low-to-high
bytes of each little-endian uint32 word); each file contains `width * height * 4`
bytes and is written after threaded rendering completes, before the frontend
callback. Output is capped at 256 MiB per capture; the receipt records bytes,
the limit, frame-output count and write status. An empty frame-output range,
limit overflow or any write error is marked incomplete. The option does not
alter the existing VDP1 frame-row coverage status.

For the same Nexus witness, add the following options to the authentic boot
command above to retain both VDP2 pass images for VDP1 frame 7716:

```sh
--trace-vdp2-render-state /path/to/dumps/nexus-nbg0.csv \
--trace-vdp2-render-state-frame-range 7716:7717 \
--trace-vdp2-render-state-row-range 0:224 \
--trace-vdp2-rendered-frames /path/to/dumps/nexus-vdp2-frames
```

An authentic frame-7716 run retained two `320x224` composed buffers, for
serials 8476 and 8477. Both contained only `0xffffffff`; the corresponding
448 row samples report NBG0 disabled on every line. The raw files, row trace,
receipts and analysis are preserved in the external evidence package at
`firestaff-evidence/nexus/ymir_nbg0_frame_serial_diagnostic_20261030/rendered_frame_capture/`.

The title-menu run using `--capture-boot-frames 9120` captured VDP1 frame
8037 twice, under VDP2 serials 9118 and 9119. Both passes contain rows and line
ordinals 0–223 exactly once, with NBG0 enabled. Their identical RGBX buffers
match the same-run frontend screenshot pixel-for-pixel after nearest-neighbor
2x downscaling; the final VDP1 register snapshot also reports frame 8037. The
receipt remains incomplete only under the one-sample-per-VDP1-frame-row
contract (448 samples for 224 frame-row pairs); its per-pass row coverage and
frame output are complete.

A shorter `--capture-boot-frames 8038` attempt did not reach VDP1 frame 8037: its
trace and pixel output were empty, even though a screenshot was saved after the
capture loop. That image is not tagged to VDP1 frame 8037. Use the VDP1 frame
range receipt to confirm the requested frame was observed before comparing
output. The successful frame-8037 buffers, screenshot, row trace, final VDP1
index and checksums are in the external evidence package under
`firestaff-evidence/nexus/ymir_nbg0_frame_serial_diagnostic_20261030/rendered_frame_capture/frame8037-authentic-target/`.

For example, this authentic Nexus capture samples rows 4 through 116 of VDP1
frame 7486 while rendering without presenting frames to the window:

```sh
ymir-sdl3 --debug --profile /path/to/nexus-profile \\
  --capture-boot-frames 8400 --dump-memory --capture-no-present \\
  --capture-pad-input 700:right:2 --capture-pad-input 720:right:2 \\
  --capture-pad-input 740:right:2 --capture-pad-input 760:right:2 \\
  --capture-pad-input 780:right:2 --capture-pad-input 800:right:2 \\
  --capture-pad-input 830:a:2 --capture-pad-input 7000:start:2 \\
  --trace-vdp2-render-state /path/to/dumps/nexus-nbg0.csv \\
  --trace-vdp2-render-state-frame-range 7486:7487 \\
  --trace-vdp2-render-state-row-range 4:117 \\
  "/path/to/Dungeon Master Nexus (Japan).cue"
```

Long cold boots can produce more than the trace buffer's 250,000-row capacity.
Add `--trace-vdp2-writes-frame-range BEGIN:END` with
`--trace-vdp2-writes` to retain writes only when their VDP1 frame index is in
that half-open interval. Both bounds are required, and the end frame is
exclusive. These are VDP1 frame indices from the CSV, not the capture-relative
frame numbers used by scheduled input. The bundle records the requested range,
write rows excluded by that range, the retention capacity, and any writes
dropped inside it. `--trace-vdp2-writes-max-rows` raises the default retention
limit of 250,000 rows to a caller-selected value from 1 through 10,000,000.
Narrowing the trace to a source-relevant interval or explicitly raising the
capacity can therefore preserve complete write provenance without changing
emulation or the final memory snapshot. The separate SH-2 data-write trace also
supports `--trace-sh2-writes-max-rows` (1 through 10,000,000) when an SH-2
address interval is selected; its receipt records the retained capacity,
address interval, frame bounds, and dropped-row count. Each SH-2 write row is
tagged with the active VDP1 frame and its CPU or DMAC origin. CPU rows retain
their instruction PC and caller context; DMAC rows carry a channel and mark
instruction PC and caller context as unknown.
`--trace-sh2-writes-frame-range BEGIN:END` restricts the trace to a half-open
VDP1 frame interval, which makes frame-local producer joins possible without
assuming that an untagged write happened in the frame of a matching VDP2 row.

The VDP2 receipt's `last_vdp1_frame_index` identifies the last recorded memory
write, while `current_vdp1_frame_index` and `vdp-register-snapshot.txt` identify
the final memory/register snapshot frame. They can differ when no VDP2 writes
occur late in a capture. Treat a later snapshot as later-frame evidence; only
claim byte stability from an earlier write frame when the complete zero-drop
trace window reaches past the snapshot frame and contains no intervening VDP2
writes. Consumers must validate those receipt bounds instead of assuming the
last-write frame and snapshot frame are identical.

For example, this captures the VDP2 writes for VDP1 frames 7,728 and 7,729
while retaining the complete final memory snapshot:

```sh
ymir-sdl3 --debug --capture-boot-frames 9140 --dump-memory \
  --trace-vdp2-writes --trace-vdp2-writes-max-rows 2000000 \
  --trace-vdp2-writes-frame-range 7728:7730
```

For a reproducible state capture without UI interaction, add
`--trace-vdp2-writes` to `--debug --load-state N --capture-frames N
--dump-memory`. The trace is enabled only for this deterministic load/frame/dump
route and is written as `vdp2-memory-write-trace.csv` with
`vdp2-memory-write-bundle.txt` in the profile's `dumps` directory. An empty
but complete trace is valid evidence that the selected capture interval made no
observed VDP2 writes; it is not a missing-output fallback.

The same CLI route also writes `sh2-dma-trace.csv` and
`sh2-dma-trace-bundle.txt`. These report the retained SH-2 DMA transfer source
and destination ranges during the frame capture, so a VDP2 destination can be
compared with the DMA source memory. The trace does not identify a CPU program
counter, prove that every VDP2 write was DMA-originated, or replace byte-level
source provenance.

It also writes `scu-dma-trace.csv` and `scu-dma-trace-bundle.txt` for SCU DMA
transfer starts. The SCU trace retains at most 1,024 entries and reports its
overflow status as unknown, so it is evidence for recorded transfers rather
than a claim that no other SCU transfer occurred.

`sh2-vdp2-write-trace.csv` and its bundle (format v2) record master or slave
SH-2 writes to VDP2 VRAM/CRAM bus addresses, including their CPU or DMAC
origin. CPU rows retain the instruction PC and current call-site; DMAC rows
include the channel and mark instruction/caller fields unknown. The trace is
bounded to 250,000 entries per SH-2 and reports dropped entries.

SH-2 memory trace v1 used a plain pc field and did not distinguish CPU
instructions from SH-2 DMAC transfers. Both kinds of access passed through the
same memory helpers, so a v1 DMA row may carry the CPU PC current after the
preceding instruction. Treat v1 PC and caller fields as untyped context, not
proof that a CPU instruction caused the access. V2 records origin explicitly
and marks DMA instruction/caller fields unknown.

`--trace-sh2-reads-begin ADDRESS --trace-sh2-reads-end ADDRESS` records
master and slave SH-2 data reads within a start-inclusive, end-exclusive
physical bus-address range during the same deterministic `--debug` frame
capture. It writes `sh2-memory-read-trace.csv` and
`sh2-memory-read-trace-bundle.txt` beside the memory dump. These files use
schema and bundle format v2. Each row has `origin=cpu` or `origin=dmac`.
CPU rows contain `instruction_pc` and the current caller return/target.
DMAC rows contain `dma_channel`; their `instruction_pc`, caller return, and
caller target are `unknown`, because transfers run asynchronously after CPU
instruction execution. Instruction fetches, debugger peeks, and accesses
outside the selected interval are not recorded. The trace retains the newest
250,000 matching reads per SH-2 and reports discarded rows. The bundle reports
CPU and DMAC row totals, including per-channel DMAC counts. Add
`--trace-sh2-read-registers` to write the optional companion file
`sh2-memory-read-register-snapshots.csv`. It contains the 16 general-purpose
registers and PC, PR, MACH, MACL, SR, GBR, and VBR immediately before each
address-matched read. Join it to the read trace with the SH-2 name and
`read_row` index. DMAC rows have `registers_available=0` and unknown register
values. Without this option, the existing read CSV and capture overhead are
unchanged.

`--trace-sh2-reads-pc ADDRESS` selects CPU data reads executed at one exact
instruction PC. It can be used by itself, or with both address-range options
to intersect the filters. Pair it with `--trace-sh2-read-registers` to capture
the source and destination registers around a retail copy instruction. PC
filter mode emits read-trace bundle v3 and adds the VDP1 frame index to both
read and register-snapshot CSV rows. Omitting the PC option preserves the v2
CSV schemas and existing address-range behavior. DMAC rows cannot match a CPU
instruction PC and are excluded in PC-filter mode.

`--trace-sh2-writes-begin ADDRESS --trace-sh2-writes-end ADDRESS` records
master and slave SH-2 data writes in an independently selected, start-inclusive
and end-exclusive physical bus-address range during that same deterministic
`--debug` frame capture. It writes `sh2-memory-write-trace.csv` and
`sh2-memory-write-trace-bundle.txt` beside the dump. These files use schema
and bundle format v2. Each row contains origin, optional DMA channel,
instruction PC when CPU-origin, physical destination address, width, and
value. The trace includes both CPU stores and SH-2 DMAC destination writes.
For DMAC rows, instruction PC and caller metadata are `unknown`; CPU rows
preserve their instruction PC and caller context. The trace retains the newest
250,000 matching writes per SH-2 and reports discarded rows. The bundle reports
CPU and DMAC row totals, including per-channel DMAC counts.

`--capture-start-frame N --capture-start-hold N`,
`--capture-start-repeat-frame N --capture-start-repeat-hold N`,
`--capture-a-frame N --capture-a-hold N`,
`--capture-a-repeat-frame N --capture-a-repeat-hold N`,
`--capture-down-frame N --capture-down-hold N`, and
`--capture-c-frame N --capture-c-hold N` can add deterministic Control Pad 1
Start, A, Down, and C presses to that same capture route. The repeat pairs schedule
second Start and A intervals. The frames are zero-based after the
state load and the hold lengths are measured in emulated frames. Each pair
requires a frame capture and `--dump-memory`; the hold must be nonzero and the
start frame must be within the capture. This is useful for reproducible
title-screen and menu investigation without relying on host window focus or
timing.

`--capture-screenshot` saves Ymir's framebuffer after the capture-owned frame
loop and memory dump. It requires a frame capture and `--dump-memory`, and
writes to the selected profile's `screenshots/` directory.

`--capture-save-state N` saves slot `N` after the same frame loop and dump.
It requires a frame capture and `--dump-memory`. The saved state is written to
the selected profile and can be loaded later with `--load-state N`, which makes
long boot or title-to-game captures practical to inspect in smaller follow-up
runs.

`--capture-no-present` is for automated deterministic captures on a host whose
window compositor cannot present frames reliably. It keeps normal renderer
initialization so emulation uses the configured video path, but deliberately
does not present GUI frames and exits after its capture-owned dump is complete.
It requires a frame capture and `--dump-memory`. It can be combined with
`--capture-screenshot`: the screenshot queue is drained before Ymir exits.
Interactive runs are unaffected.

For arbitrary button sequences, repeat `--capture-pad-input FRAME:BUTTON:HOLD`:

```sh
ymir-sdl3 --debug --capture-boot-frames 1200 --dump-memory \
  --capture-pad-input 700:right:2 --capture-pad-input 730:a:2
```

Frames are zero-based within the capture. Supported buttons are `up`, `down`,
`left`, `right`, `start`, `a`, `b`, and `c`. Holds must fit inside the capture;
button state is restored after the final captured frame. This supports repeatable
navigation through BIOS and game menus while keeping captures independent of
host window focus and input timing.

Use `--capture-boot-frames N --dump-memory` to capture directly from normal
disc boot rather than from a save-state. It is mutually exclusive with
`--capture-frames`; Start scheduling and `--trace-vdp2-writes` work with either
frame-capture route.

`-f` forces the emulator to start in fullscreen mode, ignoring the preference in Ymir.toml.

`-D` starts the emulator with debug tracing enabled.

For a frame-aligned VDP1 capture, enable **Debug > VDP > Command trace** and
then use **Debug > Dump all memory**. The dump now writes
`vdp1-command-trace.csv` and `vdp1-capture-bundle.txt` beside the VDP1/VDP2
VRAM and register files. Their frame cursor, command cursor, local coordinates,
trace row count and dropped-row count are sampled in the same emulation-thread
callback as the memory images. A nonzero `trace_dropped_total` means the
bounded trace buffer discarded earlier commands; `trace_file_complete=0`
means the CSV could not be fully written. This bundle provides frame
provenance for reverse engineering and does not assert game-specific meaning.

`-F` starts the emulator in fast-forward mode.

`-E` captures all unhandled exceptions, which can be useful to troubleshoot crashes or failure to start the emulator.

Note that the Windows version does not output anything to the console, but it does honor the command line parameters. You can pipe the output of the command to a file:

```sh
ymir-sdl3 > out.txt
```

Ymir requires an IPL (BIOS) ROM to work. You can place the ROMs under the `roms\ipl` directory created alongside the executable on the first run.
The emulator will scan and automatically select the IPL ROM matching the loaded disc. If no disc is loaded, it will use a ROM matching the first preferred region. Failing that, it will pick whatever is available.
You can override the selection on Settings > IPL.

Ymir can load game disc images from MAME CHD, BIN+CUE, IMG+CCD, MDF+MDS or ISO files. It does not support injecting .elf files directly at the moment.

When using low level CD block emulation (LLE), Ymir also requires the CD block ROM to be placed in `roms\cdb`.


## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) if the emulator crashes or misbehaves.

> [!NOTE]
> The author (@StrikerX3) works primarily with Windows and Linux systems and only provides support for other platforms on a best-effort basis.
> For Linux specifically, only the .tar.xz package on Ubuntu (both native and under WSL) is officially supported by the author.
> If you use custom-built Linux systems or anything that deviates too much from the norm, you're most likely on your own.
>
> The project relies on community support for other Linux packages, macOS and FreeBSD.


## Compiling

See [COMPILING.md](COMPILING.md).


## Support my work

If you enjoy my projects and want to help me keep developing them, consider supporting me:
- [Patreon](https://www.patreon.com/StrikerX3) for ongoing support
- PIX for one-time donations in Brazil: ask me on Discord.

Your support is completely optional but genuinely appreciated. It helps me dedicate more time and energy to these passion projects while keeping everything open-source and free for everyone. Thank you!
