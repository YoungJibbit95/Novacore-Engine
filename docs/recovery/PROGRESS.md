# Recovery progress

## 2026-10-18 baseline and rules

- Published baseline: dbdfc38d092e018952ead43d9cb669333ebf70bc.
- Rules commit: f769998; PR: https://github.com/YoungJibbit95/Novacore-Engine/pull/7.
- Worktree: F:/Coding/Project/recovery/Novacore-Engine. The original main checkout
  contains user changes (including untracked CI/benchmarks); none were imported.
- Executed: `cmake --preset local-debug-no-deps -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe`,
  `cmake --build --preset local-debug-no-deps --parallel 4`,
  `ctest --test-dir build/local-debug-no-deps --output-on-failure --timeout 120`.
- Result: complete C++23 engine/server/sandbox/test build; 5/5 suites pass.
  GCC 13.2 / Ninja / Windows; SDL3 and Vulkan disabled. Logs: build/baseline-*.log.
- Cross-repo baseline: Nemisis 6b0f411eab1a717656adfd7f70f10e2f76a5dcbf also
  builds against this engine revision, with 37/37 registered tests passing.
  Its three Vulkan-named smokes currently pass in headless mode: this is a
  pre-existing validation gap, not evidence of GPU rendering.
- Pre-existing compiler warning: unused buildSummary in EngineSandbox.cpp when
  SDL3 is disabled. No baseline test failures observed on this toolchain.
- Not run: Linux/Clang, Windows/MSVC, GPU/Vulkan and playable visual validation.
  No MSVC installation was found locally. GitHub access: ADMIN; push/PR succeeded.
- Removed: contradictory large-patch/silence rules and historical feature queues.
  No runtime code, assets, or tests removed. PR remains unmerged.

Next: branch recovery/01-ci-baseline from the rules PR, add minimal headless
Windows/MSVC and Ubuntu/Clang CI, and record real hosted results. Keep all
existing tests; do not promote a no-GPU run into graphics validation.

## 2026-10-18 CI baseline implementation

- Added shared `ci-headless` configure/build/test presets plus Linux/Clang and
  Windows/MSVC variants. GitHub workflow runs on PRs, main pushes and manual
  dispatch, with read-only permissions, cancellation/timeouts and failure logs.
- Action releases resolved through GitHub and pinned to verified commits:
  checkout v7.0.1 / 3d3c42e5aac5ba805825da76410c181273ba90b1;
  upload-artifact v7.0.2 / cf430e030ddbb5b0abf93d22962f4752f3646cd9.
- Executed: configure with `--preset ci-headless -B build/local-debug-no-deps
  -DCMAKE_CXX_COMPILER=C:/Strawberry/c/bin/g++.exe`, complete incremental build,
  all 5 CTest suites (pass), `cmake --list-presets=all`, actionlint and
  `git diff --check` (pass). Reused the baseline binary directory locally;
  hosted jobs will validate fresh platform-specific preset directories.
- No runtime code, API, assets or tests removed. The unused sandbox helper warning
  remains visible; CI does not disguise it by claiming warnings-as-errors.
- Nemisis remains pinned to the published dbdfc38 runtime; this PR changes only
  CI/presets/docs, so no engine API compatibility dependency is introduced.
- Hosted MSVC/Clang results pending publication; GPU rendering intentionally not
  covered by this CPU-only workflow. Do not mark Phase 0 complete until results
  are recorded. Next: inspect hosted runs, then classify tests/ownership in Phase 1.
- Hosted first run: Ubuntu/Clang builds and all 5 suites pass
  (https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39120015726).
  Windows-2025 could not resolve a Visual Studio 2022 instance at configure time.
  Selected the explicitly documented Windows-2022 / VS2022 runner instead;
  no tests or compiler checks were disabled. Fresh validation pending.

## Phase 0 validation complete

- Rules: f769998 / tracking 28dca6f, PR #7.
- CI: 49740d4 plus runner correction 9c87ef2, PR
  https://github.com/YoungJibbit95/Novacore-Engine/pull/8 (base: recovery/00-agent-rules).
- Fresh hosted run at 9c87ef2 passed on both platforms:
  https://github.com/YoungJibbit95/Novacore-Engine/actions/runs/39120124857.
  Ubuntu 24.04 / Clang 18.1.3: complete build, 5/5 suites pass.
  Windows 2022 / MSVC 19.44: complete build, 5/5 suites pass.
- Windows-2025 had resolved to windows-2025-vs2026, explaining the first
  VS2022-generator configure failure. Windows-2022 matches the existing toolchain.
- Actionlint and the final diff whitespace check pass. No merge performed.
- No GPU/visual validation performed. No runtime API or code changed; Nemisis CI
  continues to validate the identical published runtime at dbdfc38.

Next executable step: classify each existing test contract, then correct
`docs/13_TESTING_ACCEPTANCE.md` so weapon TTK/objective scoring/spawn safety are
Nemisis acceptance. Do this as a separate bounded PR after the CI stack. Keep
all existing coverage until its replacement/ownership is verified. Renderer,
importer and KCC extractions remain deferred behind real product needs.

## 2026-10-18 Phase 1 test architecture audit

Current status correction: GitHub confirms PR #7 merged as 9c2566d and PR #8 as
aca50b8. Earlier unmerged/pending statements above describe their original dates;
they are historical, not current instructions. Phase 1 starts from
aca50b8c1c4bec920aa4255575bb7591ca825bb0 in the clean recovery worktree. Original
user checkout remains dirty and untouched. Nemisis main is ff88eb0; its runtime
pin remains dbdfc38d092e018952ead43d9cb669333ebf70bc.

- Read all 54 test functions/assertions and CMake registrations; TEST_AUDIT.md
  records group/function categories, invariants, overlap, decisions and risk.
- Removed only four assignment-only material assertions. Neutral defaults,
  validator preservation, diagnostics and all clamp checks remain in that test.
- Corrected a no-op canonical snapshot fixture: reverse the multi-component
  entity after reversing entity order. Existing component-order checks now test
  unsorted input. No runtime code, public API, test function or suite removed.
- Updated testing acceptance to separate verified CPU behavior, missing coverage,
  real GPU requirements and Nemisis combat/movement/match ownership.
- Before changes: complete local C++23 build and 5/5 suites pass (GCC 13.2/Ninja).
  Post-change build/test and hosted MSVC/Clang results will be recorded below.
- Next: validate/publish this bounded PR; game audit and coordinate contract are
  independent Nemisis PRs. Engine pin change is unnecessary for test/doc edits.
- Post-change: complete local build passes; all 5/5 CTest suites pass with
  GCC 13.2/Ninja. `git diff --check` passes. No GPU rendering is claimed.
- Coordinate analysis also closed one engine-owned test gap: mixed parent TRS /
  child matrix plus nonuniform scale and marker forward/up. Existing tiny GLB
  writer reused; resulting function count is 55 (54 baseline + one contract).
  No importer implementation or public API changes. Independent game contract
  tests still run against the exact old pin, whose runtime behavior is unchanged.
- The added nested-transform contract also passes the complete local build and
  5/5 suites. It is engine-owned; no parser/socket repair code was introduced.
