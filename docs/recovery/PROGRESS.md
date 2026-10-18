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
