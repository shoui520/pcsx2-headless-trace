# pcsx2-trace

`pcsx2-trace` is a command-line, headless PCSX2 build for deterministic
execution tracing. It runs a BIOS, ELF, or disc through PCSX2's emulation core
and records selected PS2-visible state to versioned binary files.

It is useful for:

- checking a new CPU or VU backend against PCSX2's interpreter;
- finding the first subsystem in which two executions diverge;
- turning a long boot sequence into a repeatable replay workload;
- capturing EE, IOP, VU, VIF, SIF, GS, IPU, SPU2, memory, or scheduler state;
- building deterministic emulator regression tests without a graphical UI.

The program deliberately disables patches, cheats, speed hacks, host input,
network devices, real-time clock variation, and asynchronous GS execution. The
interpreter is used by default. Optional recompiler switches make it possible
to compare execution providers at boundaries that both providers own.

## Requirements

- A legally obtained PlayStation 2 BIOS image
- CMake and Ninja
- A C++20 compiler and the normal PCSX2 Linux build dependencies
- An ELF or disc image to execute, unless tracing the BIOS itself

BIOS images, commercial disc images, savestates, and generated traces are test
inputs or artifacts. They are not part of this repository.

## Build

From the repository root:

```sh
cmake -S . -B build/headless-trace -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_QT_UI=OFF \
  -DENABLE_TESTS=OFF \
  -DENABLE_GSRUNNER=OFF \
  -DPCSX2_TRACE_ONLY=ON \
  -DUSE_OPENGL=ON \
  -DUSE_VULKAN=OFF \
  -DX11_API=OFF \
  -DWAYLAND_API=OFF \
  -DUSE_BACKTRACE=OFF

cmake --build build/headless-trace --target pcsx2-trace --parallel
```

The executable is written to `build/headless-trace/bin/pcsx2-trace`. CMake also
places the PCSX2 resource directory beside it. Keep those resources beside the
executable if it is moved.

Check the exact options supported by the current build with:

```sh
build/headless-trace/bin/pcsx2-trace --help
```

## First trace

The basic invocation is:

```text
pcsx2-trace BIOS [ELF_OR_DISC] OUTPUT_OPTION [OPTIONS]
```

`OUTPUT_OPTION` is one or more trace flag/path pairs such as `--out ee.bin` or
`--sif-out sif.bin --core-event-out core.bin`.

For example, capture the first 50,000 EE instructions executed by a PS2 ELF:

```sh
mkdir -p traces/cube

build/headless-trace/bin/pcsx2-trace \
  /path/to/bios.bin \
  /path/to/cube.elf \
  --out traces/cube/ee.bin \
  --max-instructions 50000 \
  --data-root traces/cube/data
```

For ELF fast boot, tracing begins at the detected ELF entry point by default.
The command exits successfully after the requested record limit is reached and
prints the number of records written. `--trace-from boot` includes BIOS and
loader execution instead.

The explicit `--data-root` keeps settings and emulated persistent state local
to the test. If it is omitted, the default is a `pcsx2-trace-data` directory
beside the first trace output.

## Verify repeatability

Run the same workload with separate data roots, then compare the binary output:

```sh
TRACE=build/headless-trace/bin/pcsx2-trace
BIOS=/path/to/bios.bin
ELF=/path/to/cube.elf

mkdir -p traces/run-a traces/run-b

"$TRACE" "$BIOS" "$ELF" \
  --out traces/run-a/ee.bin \
  --max-instructions 50000 \
  --data-root traces/run-a/data

"$TRACE" "$BIOS" "$ELF" \
  --out traces/run-b/ee.bin \
  --max-instructions 50000 \
  --data-root traces/run-b/data

cmp traces/run-a/ee.bin traces/run-b/ee.bin
sha256sum traces/run-a/ee.bin traces/run-b/ee.bin
```

An empty `cmp` result and equal hashes mean the complete headers and record
streams are byte-identical. Meaningful comparisons require the same executable
revision, BIOS, guest image, starting state, command-line options, and scripted
input.

## Trace types

More than one trace can be enabled in a run. Each output is a binary stream
with its own magic, schema version, header, and fixed or explicitly framed
records.

| Option | Captured behavior |
| --- | --- |
| `--out FILE` | EE/R5900 pre-instruction architectural state |
| `--iop-out FILE` | IOP/R3000A pre-instruction architectural state |
| `--vu-out FILE` | VU0/VU1 interpreter micro-instruction state |
| `--vif-out FILE` | VIF commands and unpack effects |
| `--sif-out FILE` | SIF0/SIF1 FIFO data and tag transfers |
| `--gs-out FILE` | Decoded GS register writes, transfers, and VSync events |
| `--ipu-out FILE` | IPU commands and output hashes |
| `--spu2-out FILE` | SPU2 48 kHz mixer output |
| `--mem-out FILE` | Hashes of selected guest-memory regions |
| `--core-event-out FILE` | EE/IOP scheduling and device events |
| `--machine-checkpoint-out FILE` | Quiescent whole-machine projections after VU1 completion |

The format definitions and writers live in `pcsx2/DebugTools/*Trace.cpp`.
Trace files are intended for exact regression comparison and purpose-built
consumers; they are not text logs or a stable format across arbitrary source
revisions.

## Common workflows

### Compare interpreter and recompiler entry state

Full EE pre-instruction tracing requires the interpreter. To check another EE
execution provider at the shared ELF-entry boundary, capture exactly one entry
record from each provider:

```sh
TRACE=build/headless-trace/bin/pcsx2-trace
BIOS=/path/to/bios.bin
ELF=/path/to/test.elf

"$TRACE" "$BIOS" "$ELF" \
  --out traces/entry-interpreter.bin \
  --ee-entry-state \
  --max-instructions 1

"$TRACE" "$BIOS" "$ELF" \
  --out traces/entry-recompiler.bin \
  --ee-entry-state \
  --max-instructions 1 \
  --recompiler-ee

cmp traces/entry-interpreter.bin traces/entry-recompiler.bin
```

`--ee-entry-state` is the backend-neutral EE boundary check. It cannot be
combined with record skipping, matching, restored-state input, or boot-time
tracing. `--recompiler-iop` and `--recompiler-vu` independently select the
native IOP and microVU providers where the chosen trace types permit them. A
zero result from `cmp` proves that the captured entry records are byte-identical;
a nonzero result exposes a provider-dependent entry state for investigation.

### Include VU0 state in an EE trace

```sh
"$TRACE" "$BIOS" "$ELF" \
  --out traces/ee-vu0.bin \
  --ee-vu0-state \
  --max-instructions 100000
```

This extends each EE record with byte-exact VU0 architectural state. The larger
record schema is useful when an EE divergence may originate in COP2/VU0 work.

### Capture a late instruction window

Skip records to avoid storing a large known-good prefix:

```sh
"$TRACE" "$BIOS" "$ELF" \
  --out traces/late-ee.bin \
  --ee-skip-records 10000000 \
  --max-instructions 50000
```

Equivalent `--*-skip-records` controls exist for the IOP, memory, GS, IPU,
machine-checkpoint, SIF, core-event, SPU2, VIF, and VU streams.

### Find known EE states in a longer execution

An existing version-1 EE trace can be used as a sequence of target states:

```sh
"$TRACE" "$BIOS" "$ELF" \
  --out traces/matched-ee.bin \
  --ee-match-trace traces/reference-ee.bin \
  --max-instructions 5000000
```

The run scans up to the instruction limit and writes a record whenever the next
reference state is found. By default this compares the PC, opcode, and captured
EE architectural state. Useful variants are:

- `--ee-match-ignore-timing-state` ignores the timing-derived CP0 Count field.
- `--ee-match-pc-only` searches only by PC and opcode. This is a scouting aid,
  not a correctness oracle.
- `--mem-out FILE --mem-sample-ee-trace` hashes memory only on matched EE
  records, allowing architectural and memory state to be correlated.

### Trace communication and scheduling

This run records 2,000 SIF transfers and the scheduler/device events around
them, then stops at a scheduler-safe boundary:

```sh
"$TRACE" "$BIOS" "$ELF" \
  --sif-out traces/sif.bin \
  --core-event-out traces/core-events.bin \
  --max-sif-records 2000 \
  --stop-after-sif-limit
```

Use `--core-event-after-sif-records N` to delay core-event capture until a
specific absolute SIF record. `--ee-after-sif-records N` provides the same gate
for EE capture. These gates require an unskipped SIF stream so the record count
remains absolute.

### Trace memory without dumping RAM

```sh
"$TRACE" "$BIOS" "$ELF" \
  --mem-out traces/memory.bin \
  --mem-regions ee_ram,iop_ram,ee_scratchpad,vu0_data,vu1_data \
  --mem-hash-interval 1000 \
  --max-mem-instructions 200000
```

Use `--mem-regions all` to include every supported region. Hash traces are much
smaller than repeated raw-memory dumps and identify the interval in which a
memory divergence first appears.

### Trace GS state and draws

```sh
"$TRACE" "$BIOS" "$ELF" \
  --gs-out traces/gs.bin \
  --gs-state-snapshots \
  --max-gs-records 10000
```

`--gs-state-snapshots` adds full GS state and local-memory hash sections.
Adding `--gs-state-full` also writes raw leaf state to `gs.bin.state.bin`.
`--gs-debug-dump-dir DIR` enables PCSX2's software-renderer draw diagnostics;
those diagnostics can produce many files and are separate from the binary GS
oracle.

### Capture and replay a machine state

Portable replay states are created only at a successful terminal machine
checkpoint. This example waits for 120 completed VSync frames and then saves
the first eligible quiescent state after VU1 completion:

```sh
"$TRACE" "$BIOS" "$ELF" \
  --machine-checkpoint-out traces/checkpoint.bin \
  --machine-checkpoint-after-vsync 120 \
  --machine-checkpoint-max 1 \
  --replay-state-out traces/scene.pcsx2raw
```

Replay the seed with the same BIOS and guest image, and record the restored
state before another guest instruction executes:

```sh
"$TRACE" "$BIOS" "$ELF" \
  --replay-state-in traces/scene.pcsx2raw \
  --machine-checkpoint-out traces/restored-checkpoint.bin \
  --replay-state-checkpoint-start \
  --machine-checkpoint-max 1
```

The portable state is a validated, named-entry envelope rather than a dump of
C++ object memory. Publication is refused if the workload accesses unsupported
external-device state during the capture window. Input and output replay paths
must be distinct.

An ordinary same-build PCSX2 savestate can instead be loaded with:

```sh
"$TRACE" "$BIOS" "$ELF" \
  --pcsx2-state-in /path/to/state.p2s \
  --core-event-out traces/from-savestate.bin \
  --max-core-event-records 100000
```

### Drive a workload deterministically

Connect a neutral controller with `--pad1-dualshock2`, or generate repeatable
input without a physical controller:

```sh
"$TRACE" "$BIOS" "$ELF" \
  --core-event-out traces/input-run.bin \
  --max-core-event-records 200000 \
  --pad-autofire Cross \
  --pad-autofire-pressed-frames 2 \
  --pad-autofire-released-frames 6
```

`--pad-pulse-script` provides the built-in alternating START/CROSS sequence.
It is mutually exclusive with `--pad-autofire`.

### Boot a disc or the BIOS

Fast-boot a disc image:

```sh
"$TRACE" "$BIOS" /path/to/game.iso \
  --boot-disc \
  --sif-out traces/disc-sif.bin \
  --max-sif-records 5000 \
  --stop-after-sif-limit
```

Add `--full-boot` to take the normal BIOS disc path. To inspect the BIOS with no
ELF or disc positional argument:

```sh
"$TRACE" "$BIOS" \
  --boot-bios \
  --gs-out traces/bios-gs.bin \
  --max-gs-records 10000
```

## Limits and exit status

Most trace streams support both a record limit and a skip count. EE, memory,
GS, and VU tracing also provide instruction-scan limits so a workload that does
not produce the expected event cannot run forever. A zero event-record limit
means unlimited where accepted.

The normal successful result is reaching the requested terminal limit:

- exit `0`: capture completed at its configured limit;
- exit `1`: emulation stopped before the required limit;
- exit `2`: invalid command line;
- exit `3` or `4`: initialization, execution, trace, dump, or replay-state
  validation failed.

Always inspect the final record counts printed by the program. A generated file
alone does not prove that the intended window was reached.

## Reproducibility checklist

For an exact comparison:

1. Use the same `pcsx2-trace` commit and build configuration.
2. Use identical BIOS and guest-image bytes.
3. Start from the same savestate or replay state, if any.
4. Use identical trace, limit, gate, and input-script options.
5. Give each run a fresh, isolated `--data-root`.
6. Confirm that both processes exited successfully and reached their limits.
7. Compare the binary outputs directly with `cmp` or a cryptographic hash.

If a broad trace differs, narrow the window with skip counts, subsystem gates,
and smaller trace families. PC-only matching can locate a control-flow region;
full architectural matching, memory hashes, and machine checkpoints can then
establish whether behavior is actually equivalent.
