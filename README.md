# loxseq

Power-loss-aware step sequencer for embedded C firmware.

loxseq is a small, heap-free C99 library that runs a sequence of named steps,
persistently checkpoints the current step, and on reboot returns a recovery
verdict based on reboot reason + per-step resume policy.

## Features

- Persistent step checkpointing via caller-provided storage hooks
- Per-step resume policy (resume, restart, safe-init, operator)
- No heap, no globals, no floats

## Docs

- docs/resume-model.md
- docs/storage-contract.md
- docs/integration.md
- docs/limitations.md

## Build

- CMake: cmake -S . -B build then cmake --build build
- Tests: ctest --test-dir build

## License

MIT (see LICENSE).