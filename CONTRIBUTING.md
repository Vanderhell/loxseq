# Contributing

Thanks for considering contributing to `loxseq`.

## Development

- Configure: `cmake -S . -B build`
- Build: `cmake --build build --config Release`
- Test: `ctest --test-dir build --output-on-failure -C Release`

## Guidelines

- Keep changes focused and small.
- Prefer adding tests for behavior changes.
- Maintain C99 portability (no compiler extensions in the library).
- Avoid heap allocations, floats, and hidden global state.

