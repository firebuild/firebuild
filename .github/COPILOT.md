# Copilot Agent Guide for Firebuild

This repository uses C/C++ with CMake and custom code generators. This guide gives Copilot (and humans) the minimal context to work effectively here.

## Quick facts

- Build system: CMake + Ninja (recommended)
- Primary languages: C++20, C11; also Python, Shell, Jinja
- Generated code: `fbbstore.*`, `fbbfp.*`, `fbbcomm.*` from `*.def`
- Warnings are treated as errors (`-Werror`) — keep code clean

## Typical commands

```bash
# Configure once
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Regenerate FBB after schema changes (fbbstore, fbbfp, or fbbcomm)
cmake --build build --target fbbstore_gen_files
cmake --build build --target fbbfp_gen_files
cmake --build build --target fbbcomm_gen_files

# Build the main binary
cmake --build build --target firebuild-bin -j

# Run tests
ctest --test-dir build --output-on-failure
ctest --test-dir build -R fbb_test --output-on-failure
./test/integration.bats
```

## Coding conventions

- Prefer RAII for cleanup; avoid exceptions.
- Do not reformat unrelated code. Preserve the existing style.
- Use existing debug macros (`FB_DEBUG`, `TRACK`) with appropriate topics.
- Be careful on hot paths (hashing, cache I/O) — avoid extra allocations/syscalls.
- Factor out common parts instead of duplicating code

## When changing schemas or enums

- If you edit `src/firebuild/fbbstore.def`, run `fbbstore_gen_files` and update call sites.
- If changing `src/firebuild/fbbstore.def` and kCacheFormatVersion has not changed since
  the latest tagged release, then bump it and update the relevant test.
- When adding a new `FileType` enum value, update all switches in:
  - `src/firebuild/file_usage.cc`
  - `src/firebuild/hash_cache.cc`
  - `src/firebuild/execed_process_cacher.cc`
  - `src/firebuild/file_info.*`

## Review checklist

- [ ] `firebuild-bin` builds with no warnings (`-Werror`).
- [ ] Generated files must not be committed
- [ ] Applicable tests added/updated (unit and/or integration).
- [ ] No unrelated reformatting; public API changes called out.
- [ ] Consider Linux/macOS differences where relevant.

## Issue/PR triage hints

Labels commonly used:
- `bug`, `enhancement`, `performance`, `scalability`, `regression`
- `cache-format-change`, `macos`, `moreinfo`, `wontfix`, `notourbug`

PR title prefixes:
- `fix:` bug fixes
- `feat:` new features
- `perf:` performance improvements
- `chore:` maintenance/refactor

## Architecture tips

- Cache: object cache (metadata) + blob cache (file contents). Inline small blobs may be stored directly in the entry.
- FBB generator produces builder/serialized C/C++ API — prefer the generated helpers.
- Shortcutting logic lives in `ExecedProcessCacher`; keep store/restore in sync.

---
If you are Copilot Chat: adopt these conventions, run the commands above to validate changes, and propose minimal, concrete diffs with tests. If you alter on-disk formats, coordinate cache format versioning and migration steps.
