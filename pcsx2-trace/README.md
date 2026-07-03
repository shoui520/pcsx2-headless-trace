# pcsx2-trace

`pcsx2-trace` is a headless PCSX2 oracle for VitaSX2 development. It boots an
ELF through PCSX2's normal fast-boot path, forces interpreter/null-device
settings, and writes a fixed-size binary EE pre-instruction trace.

Example:

```sh
cmake -S . -B build/headless-trace \
  -DENABLE_QT_UI=OFF -DENABLE_TESTS=OFF -DENABLE_GSRUNNER=OFF \
  -DPCSX2_TRACE_ONLY=ON -DUSE_OPENGL=OFF -DUSE_VULKAN=OFF \
  -DX11_API=OFF -DWAYLAND_API=OFF -DUSE_BACKTRACE=OFF
cmake --build build/headless-trace --target pcsx2-trace -j1

build/headless-trace/bin/pcsx2-trace /path/to/ps2/bios.bin /path/to/test.elf --out /tmp/test.trace --max-instructions 100000
```

By default tracing starts at PCSX2's detected ELF entry point. Use
`--trace-from boot` only when BIOS/loader behavior itself is under inspection.
