# Changelog

All notable release-facing changes are documented here.

## [1.0.0-rc.1] - 2026-03-31

### Release engineering

- **Versioning**: `package.json` and root `VERSION` aligned at `1.0.0-rc.1`.
- **Makefile**: macOS uses `brew --prefix` when set; Linux uses `pkg-config` for SDL2, Jansson, libwebsockets, libmicrohttpd, and links `-lGL`. Removed duplicate `src/gpu_physics.o` rule and duplicate `-ljansson` on `verse-client`.
- **CI**: Added `.github/workflows/native.yaml` (`make release-check-native` on Ubuntu and macOS). Updated Node workflow actions; Codecov upload is non-blocking without a token.
- **Quality**: `make release-check-native` runs headless C tests (`test-bulk-ops`, `test-universe-consistency`, `test-refactored-worlds`) then builds `verse-client`. Fixed `test-refactored-worlds` link line and composite-filter usage in `test_bulk_ops.c` for strict C compilers.
- **Git**: `.gitignore` no longer ignores the entire `tests/` tree; ignores `*.o` and common test artifacts under `tests/`.
- **Scripts**: `npm run release:verify` runs native checks plus `npm ci`, `build`, and `test:ci` (Puppeteer-free). Use `npm test` locally for the full browser stack; `npm run release:verify:native` is C-only.

### Notes

- Story and final art assets are still external to this tag; engine and tooling are release-candidate hardened for integration.
