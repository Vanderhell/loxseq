# Release checklist

## Source hygiene
- `CHANGELOG.md` updated under `[Unreleased]`
- public header version macros match the intended release version
- docs updated (`README.md`, `docs/*.md`) as needed for behavior/API changes

## Build + test (local)
- `cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug`
- `cmake --build build-debug`
- `ctest --test-dir build-debug --output-on-failure`
- `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release`
- `cmake --build build-release`
- `ctest --test-dir build-release --output-on-failure`

## Install smoke test
- `cmake --install build-release --prefix dist`
- (optional) build a tiny consumer project with `find_package(loxseq CONFIG REQUIRED)`

## Examples smoke test (host)
- build examples (`LOXSEQ_BUILD_EXAMPLES=ON`)
- run `loxseq_example_minimal` and `loxseq_example_batch_reactor` where supported

## CI
- CI workflow green on Ubuntu (GCC/Clang), Windows (MSVC), macOS (Clang)
- sanitizer job green (ASan/UBSan)

## Tag + release
- tag name matches version (e.g. `v0.1.0`)
- GitHub release artifacts created and downloadable

