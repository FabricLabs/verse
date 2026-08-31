# VERSE Release Review — August 2026

Comprehensive review performed on return from hiatus. Every claim below was verified
against the working tree on macOS (arm64, clang, Node v24.15.0) on 2026-08-24.

Companion document: [RELEASE_PLAN.md](RELEASE_PLAN.md).

---

## Headline

**The engine works. The repository does not contain it.**

`make release-check-native` passes. `verse-client` builds and runs. But `HEAD` contains
**zero files under `src/`**, and not one of the 26 source files that link into
`verse-client` has ever been committed. The release candidate is real, and it exists on
exactly one disk.

Everything else in this review is secondary to that fact.

---

## Where the project actually stands

| | |
|---|---|
| Last commit | `8e52473` — 2024-01-18, "Update package-lock.json" |
| Last engineering activity | 2026-04-01 (`package-lock.json`); `CHANGELOG.md` dated 2026-03-31 |
| Effective hiatus | ~5 months |
| Current branch | `feature/a-frame` |
| Declared version | `1.0.0-rc.1` (`VERSION` and `package.json` agree) |

The 2026-03-31 CHANGELOG entry describes a completed release-engineering pass: version
alignment, Makefile portability, two CI workflows, a native release gate, and a
`.gitignore` revision. That work is genuinely done and good. **It was simply never
committed.**

So the project is not "half finished." It is a working release candidate sitting
outside version control.

---

## 1. Version control — critical

`git status` reports 130 tracked files against 21,177 untracked ones.

### The shipping client is not in the repository

`verse-client` links 26 C sources (`CLIENT_SOURCES`, `Makefile:78-85`):

| State | Count | Files |
|---|---|---|
| Committed | **0** | — |
| In the index / modified, never committed | 7 | `verse_client.c`, `window.c`, `world.c`, `isometric_renderer.c`, `voxel_mesh.c`, `wfc.c`, `world_spawn.c` |
| Not in git at all | 19 | `game_state.c`, `character.c`, `actor.c`, `fp_renderer.c`, `octree.c`, `engine.c`, `input_manager.c`, `settings.c`, `world_bulk_ops.c`, `world_transition.c`, `greedy_mesh.c`, `model_transformer.c`, `gpu_voxel_buffer.c`, `background_music.c`, `title_hum.c`, `ui_sounds.c`, `synthesizer/synthesizer.c`, `songwriter/songwriter.c`, `sequencer/melody_loader.c` |

The 7 staged files exist as blobs in `.git/objects` and would survive a working-tree
loss. The other 19 would not.

### Scope of unversioned work

- **248** first-party `.c`/`.h` files under `src/` are untracked — **70,436 lines**
  (excludes vendored `noise-c/` and `libwebsockets/`).
- **28** tracked files carry uncommitted diffs: **+30,363 / −4,879**.
- `scripts/release-verify.sh` — the release script itself — is untracked.
- Largest unversioned sources: `world_editor_ui.c` (2,309), `world_viewer.c` (2,207),
  `window_interactive_test.c` (1,953), `ui_system.c` (1,854), `fp_renderer.c` (1,789),
  `voxel.h` (1,741), `proxy.c` (1,569).

### Nothing is preserved anywhere else

- `src/` is **not** in `.gitignore` — these files were simply never added.
- No remote branch contains `src/game_state.c` (checked `origin/{master,develop,engine,fabric}`
  and `upstream/{master,develop,feature/a-frame}`).
- The three `git stash` entries are from the 2024-era JavaScript work, unrelated.
- `.archive/` is empty.
- Remote: `git@github.com:martindale/verse.git` (`upstream` = `FabricLabs/verse`).

### Committing is not as simple as `git add -A`

The working tree is **6.5 GB**. A naive add would attempt to commit:

| Path | Size | Nature |
|---|---|---|
| `assets/` | 2.0 GB | mixed — includes 332 MB `assets/pokemondb.net/` scraped sprite data |
| `worlds/` | 485 MB | 1,040 generated `.world` files |
| `world_analysis/` | 348 MB | 102 generated `.txt` dumps |
| `android/.gradle/` | — | Gradle build cache |

Seven individual files exceed 50 MB, including two `worlds/*.world` files and
`assets/universe_top_voxel.bmp` — GitHub rejects pushes containing files over 100 MB,
and repository bloat is effectively permanent.

By contrast, `verse-client` needs very little at runtime: `assets/fonts/visitor-tt2-brk.ttf`
(the `assets/fonts/` directory is 40 KB), `assets/melodies/` (24 KB), `assets/sounds/`
(4.6 MB), and `settings.dat`.

`.gitignore` must be written **before** anything is staged. The 332 MB
`assets/pokemondb.net/` tree also warrants a licensing look before it goes anywhere near
a public repository.

---

## 2. Build and test health — good

This is the strong part of the project.

### Native build

`make` (default target `all`) completes with **0 errors** and **213 warnings**.

### Native release gate

`make release-check-native` **passes**. It runs three headless suites and then builds the
client:

| Suite | Source | Covers |
|---|---|---|
| `test-bulk-ops` | `src/test_bulk_ops.c` | region/shape fills, type/height/noise/composite filters, world merge, batch ops, 32³ benchmarks |
| `test-universe-consistency` | `src/test_universe_consistency.c` | universe init, standalone and positioned generation, placement, save/load with universe context |
| `test-refactored-worlds` | `src/test_refactored_worlds.c` | HOME/ARENA/FARM generation with voxel-count assertions |

This is a legitimate, meaningful release gate. It is the single most valuable asset the
previous release pass produced.

### `make verse` is broken

The server/CLI target fails to link with 12 undefined symbols. `CORE_SOURCES`
(`Makefile:73-74`) is missing six objects:

| Missing object | Undefined symbols it provides |
|---|---|
| `src/world_bulk_ops.c` | `world_fill_region`, `world_fill_half_sphere`, `world_fill_layered_terrain`, `world_apply_noise_pattern`, `voxel_filter_type` |
| `src/entropy_field.c` | `entropy_field_sample`, `entropy_field_custom` |
| `src/universe.c` | `universe_ensure_seed_consistency` |
| `src/universe_coords.c` | `get_world_universe_coords`, `get_world_universe_coords_f` |
| `src/mud_telnet.c` | `run_mud_telnet_server` (called from `main.c`) |
| `src/constants.c` | `condition_bit_from_name` |

Adding those six to `CORE_SOURCES` is likely the whole fix. Note that `world_fill_region`
is defined in **three** places — `world_bulk_ops.c:200`, `world_voxel.c:222`, and
`test_generation_module.c:17` — so the decomposition work needs a decision on ownership
before linking, or the duplicate symbols will collide.

### JavaScript / web stack

`node_modules` is **empty**; the JS side is currently uninstalled and unrunnable.

Four dependencies resolve over **`git+ssh://`** in `package-lock.json`:

| Package | Pinned commit | Reachable |
|---|---|---|
| `@fabric/core` (`FabricLabs/fabric`) | `62f85623` | HTTP 200 |
| `@fabric/http` (`FabricLabs/fabric-http`) | `82fede12` | HTTP 200 |
| `jsdoc` (`FabricLabs/jsdoc`) | `1a9aee2a` | HTTP 200 |
| `three-bmfont-text` (`dmarcos/…`, transitive via `aframe`) | `eed48787` | HTTP 200 |

All four commits still exist, so the supply chain is intact. But `npm ci` needs
git-over-SSH credentials to fetch them, which is a standing risk for any clean CI
environment. This should be proven in CI rather than assumed.

There is also a Node version disagreement: `.nvmrc` pins **18.19.0** (and CI reads
`.nvmrc`), while `@fabric/core` declares `engines.node: "22.14.0"`. Installing under
Node 24 produced an `EBADENGINE` warning rather than a failure, so it is a warning today
and a question mark tomorrow.

### JS test coverage is thin

- `npm test` → 6 cases across 4 files.
- `npm run test:ci` → 5 cases across 3 files (`encounter.unit.js`, `schemata.js`,
  `verse.core.js`).
- `tests/client.js` is excluded from CI because `@fabric/http`'s `Sandbox` launches
  Puppeteer.
- Last recorded coverage (2026-03-31, JS `types/` layer only): **60.48%** statements,
  62.5% branches, **29.54%** functions.

These 5 tests cover the thin JS wrapper, not the ~86k-line C engine. The C engine's real
coverage is the three native suites above.

---

## 3. Neither CI workflow can currently pass

Both workflows are well written. Both are aimed at a repository state that does not exist.

| | `native.yaml` | `test.yaml` |
|---|---|---|
| Triggers | push/PR on `master`, `main` | push/PR on `master` only |
| Runners | ubuntu-latest, macos-latest | ubuntu-latest, macos-latest |
| Does | system deps → `make release-check-native` | `npm ci` → `npm run report:coverage:ci` → Codecov (non-blocking) |

`native.yaml` runs `make release-check-native` immediately after `actions/checkout@v4`.
Because `src/` is absent from git, the checkout yields no engine sources and the build
fails at once. It has almost certainly never run green — and it would not have been
noticed, because neither workflow triggers on `feature/a-frame`, where all the work
lives.

Secondary gaps: `test.yaml` omits `main`; Codecov is `continue-on-error` and uploads
without a token, so coverage regressions cannot fail a build.

---

## 4. Game completeness — `STATUS.md` is out of date

`STATUS.md` claims only the title screen is done. The code says otherwise in both
directions.

| Checklist item | `STATUS.md` | Actual | Notes |
|---|---|---|---|
| Title screen | `[x]` | **Complete** | Fade-in, pulsing subtitle, title hum; `window.c:3226-3295` |
| Main menu | `[ ]` | **Complete** | `window_render_main_menu` (`window.c:1110-1198`), keyboard + mouse, settings, exit modal. Should be `[x]` |
| Player registration | `[ ]` | **Absent** | Server-side only: `proxy.c:1142-1203` handles `CREATE_CHARACTER` over WebSocket. No client UI |
| Character creation | `[ ]` | **Partial** | Name entry only (`window.c:524-578`). No attributes; `character_save_game()` is never called on the new-game path |
| Game introduction | `[ ]` | **Partial** | Hardcoded placeholder string; `scenes/00000-genesis.json` and `chapters/00000-genesis.json` are never loaded |
| Tutorial quest | `[ ]` | **Stubbed** | `window_init_tutorial_quest()` exists but is never called; `quests/00000-humble-beginnings.json` has no loader |
| Chapter 1 cinematic | `[ ]` | **Partial** | Typewriter text panel with fade, not a cinematic; player spawns in wilderness, not the Garden |

### Playable today

Title → main menu → new game with name entry → async generation of 54 worlds →
skippable intro text → voxel world exploration with movement, isometric/first-person
toggle, and settings.

That is a real vertical slice.

### The screen state machine is half-populated

Of 11 `GAME_SCREEN_*` states, 6 have working render and input paths (`MAIN_MENU`,
`WORLD`, `IN_GAME_MENU`, `SETTINGS`, `CHAPTER`, `LOADING`). Four are enum values only:
`BATTLE`, `CHARACTER`, `INVENTORY`, `SCENE`. The title screen sits outside the enum as a
`g_show_title_screen` overlay.

### Save/load is the most visible functional gap

- In-game "Save Game" prints "not implemented yet" (`verse_client.c:409`).
- "Continue" starts a new game instead of loading (`verse_client.c:467`).
- `game_state_load_game()` has `TODO: Load player save data` and falls through to
  `game_state_start_new_game()` (`game_state.c:220`).
- The main menu renders **two** "Continue" buttons when saves exist
  (`BUTTON_CONTINUE` at `window.c:1156` and `BUTTON_LOAD_GAME` at `1166`).

The `character.c` save/load and `PlayerId` infrastructure is written but disconnected
from the client flow.

### There is a better prototype than the shipping client

`src/window_interactive_test.c` (1,953 lines) already implements scene JSON loading, a
four-page Genesis narrative, quest boxes, and character save-on-create. It is not built
by any Makefile. For the onboarding work, this is a port, not a green-field
implementation.

---

## 5. Code quality

### 213 warnings, and most are cosmetic

| Warning | Count | Assessment |
|---|---|---|
| `-Wunused-parameter` | 50 | cosmetic |
| `-Wunused-variable` | 32 | cosmetic |
| `-Wsign-compare` | 29 | worth auditing — indexing/loop bounds |
| `-Wunused-function` | 28 | dead code signal |
| `-Wincompatible-pointer-types` | 24 | **real** |
| `-Wformat` | 15 | **real** |
| `-Wunused-but-set-variable` | 14 | possible logic bugs |
| `-Wunused-const-variable` | 8 | cosmetic |
| `-Wincompatible-pointer-types-discards-qualifiers` | 5 | `const` correctness |
| others | 8 | minor |

### The pointer warnings are one root cause, not 24 bugs

`synthesizer.h:148` and `universe.h:21` both close **anonymous** structs:

```c
typedef struct { /* ... */ } Synthesizer;
typedef struct { /* ... */ } Universe;
```

Consumers then forward-declare a *named* tag — `title_hum.h:11` even comments its intent:

```c
// Forward-declare Synthesizer without redefining typedef
struct Synthesizer;
```

`struct Synthesizer` and `Synthesizer` are therefore unrelated incomplete types. Every
call across that boundary is a type mismatch: 19 in `title_hum.c`, 2 in `universe.c`,
2 in `world.c`. It works only because both are pointers.

The fix is to name the struct tags (`typedef struct Synthesizer { … } Synthesizer;`),
which is one line per header. Worth doing promptly — GCC 14 and Clang 16 have been
promoting this class of warning toward an error, so it is a portability cliff, not just
untidiness.

Separately, `isometric_renderer.c:2184/2337/2344` and `window.c:3511` pass **anonymous
struct** types across function boundaries. Those are genuinely distinct types and need
real named declarations.

The `-Wformat` warnings are all `%lu` against `uint64_t`; the fix is `PRIu64`. Benign on
64-bit macOS/Linux, wrong on 32-bit targets.

### Memory safety is unaudited

- **271** `malloc`/`calloc`/`realloc` sites in `src/*.c`.
- **38** `strcpy`, **22** `sprintf`, **6** `strcat` — against 317 `snprintf` and 83
  `strncpy`, so the codebase mostly does the right thing but not consistently.
- No `gets` or `alloca`.
- `AGENTS.md` lists `valgrind pass` as an open TODO. Nothing indicates it was started.

The 2025-era `NASA_EVALUATION_REPORT.md` rated memory safety and security "High Risk."
Nothing in the tree suggests those findings were addressed, though its conclusions read
as generic and were not re-derived here.

### Orphaned and duplicated code

- **61** `.c` files (~15,000 lines) are referenced by **no** Makefile anywhere.
- **104** of 174 `.c` files are absent from the root Makefile (43 are reachable via
  `src/Makefile.*` variants).
- `world.c` is **7,793 lines**. A decomposition into `world_core.c`, `world_voxel.c`,
  `world_noise.c`, `world_serialize.c`, and ten `world_generation_*.c` files exists but
  is not wired into the client — so both versions must be maintained.
- A parallel `verse_client_modular` plus seven `client_*.c` modules represents a second
  unfinished refactor.
- Literal backups in the tree: `world.c.backup`, `window.c.backup`, `fp_renderer.c.bak`,
  `cpu_render_phases_test.c.backup`, `scripts/browser copy.js`, `scripts/build copy.js`.
- Three universe-map generators and three entropy-test variants coexist.

Two abandoned refactors in flight is the main structural risk to velocity. Neither is
finished, and both compete with the monolith they were meant to replace.

---

## 6. Documentation debt

**61 markdown files at the repository root. Three are tracked.**

Roughly 50 are AI-generated session artifacts from Aug–Sep 2025 — `DECOMPOSITION_PROGRESS.md`
through `DECOMPOSITION_PROGRESS_7.md` and `_FINAL`, plus `FINAL_TERRAIN_IMPROVEMENTS.md`,
`FINAL_HEIGHT_VARIATION_AND_STONE_TYPES_FIX.md`, `NATURAL_TERRAIN_FINAL.md`,
`SEDIMENT_AND_CAP_FIXES_SUMMARY.md`, and similar.

They record superseded intermediate states, they contradict each other, and their
filenames give no ordering. The eight `DECOMPOSITION_PROGRESS*` files describe a refactor
that never shipped. Anyone returning to this project — human or agent — will read them as
current and be misled.

`EXECUTIVE_SUMMARY.md` and `NASA_EVALUATION_REPORT.md` propose an "$300,000 over 8
months, 4 developers" hardening program, complete with a `Date: [Current Date]`
placeholder and a fictional author line. That is roleplay output, not an engineering
assessment, and it should not be mistaken for a plan of record.

Genuinely useful and worth keeping: `AGENTS.md`, `CHANGELOG.md`, `GAME.md`, `WORLD.md`,
`FABRIC.md`, `README.md`, `DEVELOPERS.md`, `NEWBIE.md`, `reports/memory_report.md`.

### Also untracked

`.DS_Store` (plus copies under `assets/`), `.idea/`, `.vscode/`, ~46 compiled executables
at the repository root, 147 binary artifacts overall, and root-level scratch output
(`edge_analysis.txt`, `high_slice.txt`, `benchmark_results.txt`, `*.obj` mesh dumps,
`actors.dat`, `settings.dat`).

---

## Risk register

| # | Risk | Severity | Basis |
|---|---|---|---|
| 1 | Entire engine unversioned; single-disk existence | **Critical** | 0 files under `src/` in `HEAD`; 70,436 unversioned lines; nothing on any remote |
| 2 | Naive `git add` bloats repo irreversibly or fails the push | **High** | 6.5 GB tree; 7 files >50 MB; 833 MB of generated worlds and analysis |
| 3 | Both CI workflows structurally cannot pass | **High** | `native.yaml` builds a checkout with no sources; neither triggers on `feature/a-frame` |
| 4 | `assets/pokemondb.net/` licensing | **High** | 332 MB of third-party scraped sprite data staged for a public repo |
| 5 | Memory safety unaudited | **Medium** | 271 alloc sites, 38 `strcpy`, 22 `sprintf`; valgrind never run |
| 6 | Type-mismatch portability cliff | **Medium** | 24 pointer warnings from anonymous typedefs; newer compilers escalate these |
| 7 | `make verse` does not link | **Medium** | 12 undefined symbols; 6 objects missing from `CORE_SOURCES` |
| 8 | Two unfinished refactors compete with the monolith | **Medium** | `world.c` decomposition and `verse_client_modular`, neither wired in |
| 9 | Save/load absent; "Continue" is a stub | **Medium** | Most visible functional gap for a player |
| 10 | `npm ci` needs git-over-SSH in CI | **Medium** | 4 `git+ssh://` deps; commits reachable but auth unproven in CI |
| 11 | Documentation actively misleads | **Medium** | 61 root docs, ~50 superseded; `STATUS.md` wrong in both directions |
| 12 | Node version disagreement | **Low** | `.nvmrc` 18.19.0 vs `@fabric/core` `engines` 22.14.0 |

---

## Assessment

The engineering is in better shape than the repository suggests. There is a working
voxel engine, a passing native release gate, a real playable slice, and a
well-constructed CI configuration. The 2026-03-31 release pass did the right work.

The problem is that none of it is committed, so none of it is verifiable, reproducible,
or safe — and the CI built to prove it cannot run. Nearly every other finding in this
review is either a consequence of that (CI can't pass, docs drifted, artifacts
accumulated) or a modest, well-understood cleanup.

Recovery is mostly bookkeeping rather than engineering, and the highest-value hour
available is the one that gets `src/` into git behind a correct `.gitignore`.

See [RELEASE_PLAN.md](RELEASE_PLAN.md).
