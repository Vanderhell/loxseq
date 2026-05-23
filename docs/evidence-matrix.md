# Evidence matrix

This file maps repository claims to concrete evidence (tests, source inspection,
and CI).

| Claim | Evidence |
|---|---|
| No heap allocation | Source inspection: `include/` + `src/` contain no `malloc/calloc/realloc/free` usage. |
| No floating point required | Source inspection: `include/` + `src/` contain no `float`/`double` usage. |
| Caller-owned state, no hidden globals | Source inspection: `loxseq_t` is caller-supplied; implementation uses no mutable global runtime state. |
| Recovery restart resets step timer | Unit test: `test_start_resume_restart_step_entered_semantics()` and `test_operator_resolve_restart_resets_step_entered()` in `tests/test_loxseq.c`. |
| RESUME preserves step timer | Unit test: `test_start_resume_restart_step_entered_semantics()` in `tests/test_loxseq.c`. |
| `last_recovery` reflects last verdict | Unit test: `test_recover_sets_last_recovery()` in `tests/test_loxseq.c`. |
| Reboot counter saturates at `0xFF` based on checkpoint | Unit test: `test_reboot_counter_increment_and_saturation()` in `tests/test_loxseq.c`. |
| Record layout is as expected on hosted CI compilers | Compile-time assertions in `tests/test_loxseq.c`. |
| Basic sequencing behaviors work (branching, timeouts, pause, abort, complete) | Unit tests in `tests/test_loxseq.c`. |
| Cross-platform build/test coverage | GitHub Actions workflow: `.github/workflows/ci.yml`. |

