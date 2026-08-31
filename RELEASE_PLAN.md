# VERSE Release Plan

Plan of record for shipping `1.0.0`. Derived from [RELEASE_REVIEW.md](RELEASE_REVIEW.md)
(2026-08-24).

Supersedes `IMPLEMENTATION_PLAN.md`, `EXECUTIVE_SUMMARY.md`, `NASA_EVALUATION_REPORT.md`,
`WILDERNESS_OPTIMIZATION_PLAN.md`, and `DECOMPOSITION_PLAN.md`.

---

## Scope: two releases, not one

`CHANGELOG.md` already draws the right line for `1.0.0-rc.1`:

> Story and final art assets are still external to this tag; engine and tooling are
> release-candidate hardened for integration.

Keeping that boundary is what makes this plan finishable.

**`1.0.0-rc.1` — engine and tooling.** Everything needed to build, verify, and reproduce
the engine from a clean checkout. No new gameplay. Substantially already written; the work
is getting it into git and proving it in CI.

**`1.0.0` — playable game.** The `STATUS.md` onboarding path end to end: registration
decision, character creation, Genesis intro, tutorial quest, working save/load.

Phases 0–3 ship `rc.1`. Phases 4–5 ship `1.0.0`. Phase 0 gates everything.

---

## Phase 0 — Preserve the work

**Gate. Nothing else starts until this is done.** Target: one sitting.

70,436 lines of engine source exist on one disk, in no commit, on no remote. This phase
is pure risk reduction and involves no code changes.

### 0.1 Snapshot outside git first

Before touching the tree, copy it somewhere else — external disk or another machine.
A tarball excluding `worlds/`, `world_analysis/`, `node_modules/`, `android/.gradle/`,
and `assets/pokemondb.net/` is a few hundred megabytes and takes minutes. This is the
cheapest insurance available and it protects against a mistake during 0.2.

### 0.2 Write `.gitignore` before staging anything

The tree is 6.5 GB with seven files over 50 MB. Getting this wrong bloats the repository
permanently or fails the push outright. Ignore rules must land **first**.

Needs to cover:

- Generated data: `worlds/`, `world_analysis/`, `converted/`, `build/`
- Build outputs: `*.o`, `*.a`, `*.so`, `*.dylib`
- The ~46 extensionless binaries at the repository root
- Editor and OS noise: `.DS_Store`, `.idea/`, `.vscode/`
- Scratch output: root `*.obj` mesh dumps, `edge_analysis*.txt`, `*_slice*.txt`,
  `benchmark_results.txt`, `actors.dat`, `settings.dat`
- `android/.gradle/`, `android/build/`
- Backup files: `*.backup`, `*.bak`, `* copy.*`

Then verify before committing:

```bash
git add -An | wc -l                                    # sanity-check the count
git add -An | sed 's/^add .//;s/.$//' | \
  xargs -I{} du -k {} 2>/dev/null | sort -rn | head -20 # largest staged files
```

Nothing over ~10 MB should appear. Because the root binaries have no extensions, the
ignore list has to name them — which is brittle. **Recommended:** change the Makefile to
emit binaries into `bin/` and ignore `bin/` wholesale. That converts ~46 fragile rules
into one durable one. Best done now, while nothing is committed.

### 0.3 Resolve the `assets/` question

`assets/` is 2.0 GB and cannot be committed as-is. It needs splitting:

- **Commit** what the client loads at runtime: `assets/fonts/` (40 KB),
  `assets/melodies/` (24 KB), `assets/sounds/` (4.6 MB), `assets/styles/`,
  `assets/images/`, `assets/tracks/`.
- **Ignore** generated renders: the `cpu_phase_*.bmp`, `cpu_render_phase_*.bmp`,
  `sprite_phase_*.bmp`, `topdown_*.bmp`, `current*.bmp`, `reference*.bmp`,
  `universe_*.bmp` families, and `assets/bundles/` (19 MB webpack output).
- **Decide separately** on `assets/pokemondb.net/` — 332 MB of third-party scraped
  sprite data. Too large to commit and a licensing exposure in a public MIT repository.
  Recommend excluding it and sourcing sprites deliberately. Flagged as its own decision
  below.

### 0.4 Commit in reviewable slices

One 70k-line commit is unreviewable and unbisectable. Suggested sequence, verifying the
build between each:

1. `.gitignore` and the `bin/` Makefile change
2. Vendored dependencies (`src/noise-c/`, `src/libwebsockets/`) — isolated so later
   history stays readable
3. Engine core: `world*.c/h`, `voxel*`, `octree`, `universe*`, `entropy_field`, `wfc*`
4. Rendering: `isometric_renderer`, `fp_renderer`, `voxel_mesh`, `greedy_mesh`,
   `model_transformer`, `gpu_voxel_buffer`
5. Audio: `synthesizer/`, `songwriter/`, `sequencer/`, `background_music`, `title_hum`,
   `ui_sounds`
6. Client and game logic: `verse_client`, `game_state`, `window`, `character`, `actor`,
   `input_manager`, `settings`, `world_transition`, `world_spawn`
7. Tools and editors: `world_editor*`, `world_viewer`, `universe_*`, `asset_converter`,
   `world_tool`
8. Native tests: `test_bulk_ops`, `test_universe_consistency`, `test_refactored_worlds`
9. Build and release infrastructure: `Makefile`, `scripts/`, `.github/workflows/`
10. Runtime assets from 0.3
11. Documentation, after the Phase 1 cleanup

Defer the 61 orphaned `.c` files and the two unfinished refactors to Phase 2 — decide
their fate rather than committing them by reflex.

### 0.5 Push

Push `feature/a-frame` to `origin`. **Phase 0 is not complete until the work exists on a
second machine.**

**Exit criteria**

- `git status --porcelain` shows no untracked first-party source
- Fresh `git clone` into a temporary directory → `make release-check-native` passes
- `du -sh .git` remains proportionate (expect low hundreds of MB, not gigabytes)
- Branch exists on `origin`

That clean-clone check is the real test: it is precisely what CI does, and what has never
worked.

---

## Phase 1 — Make CI honest

Target: 1–2 days. Everything here is verification, not feature work.

### 1.1 Get `native.yaml` green

It should pass as written once sources exist. Add `feature/a-frame` (or whichever branch
is now primary) to its triggers so it actually runs, and use `workflow_dispatch` to
confirm before relying on it.

Watch for: Linux `pkg-config` paths diverging from macOS `brew --prefix`, and warnings
that clang tolerates but GCC rejects — the 24 pointer-type mismatches from §5 of the
review are the likeliest Linux failure, since GCC 14 treats several as errors. Fixing
those (Phase 2.1) may be a prerequisite rather than a follow-up.

### 1.2 Prove `test.yaml`, or cut it down

The open question is whether `npm ci` can fetch four `git+ssh://` dependencies on a clean
runner. Resolve it empirically rather than by reasoning. If SSH fetching fails, options in
descending preference: rewrite the lockfile to `https://` remotes; vendor the Fabric
packages; or drop `aframe` if `three-bmfont-text` is the only blocker and A-Frame is no
longer load-bearing.

Also settle `.nvmrc` (18.19.0) against `@fabric/core`'s `engines.node` (22.14.0). Pick one
and make both agree.

Add `main` to the trigger list.

### 1.3 Extend the native gate — **partly done**

First, a prerequisite that was more serious than the gate's coverage: **the gate could not
fail.** Every suite in it printed its results and then returned 0 unconditionally.
`test_bulk_ops.c` discarded 12 `success` booleans; `test_universe_consistency.c` printed
6 `✗` branches; `test_refactored_worlds.c` discarded 3. `universe_entropy_test.c` was
actively printing `✗ World content generation test FAILED` on every run while exiting 0,
and had been for as long as it has existed.

All four now count failures and return non-zero. Verified by forcing a failure into
`test_bulk_ops` and confirming the gate aborts (exit 1) rather than reporting OK.

The content-generation failure it had been hiding was a bad expectation, not a
product bug: the test sampled a 10×10×10 corner at `z ∈ [0,10)` of a layered wilderness
world, which is entirely underground by design, then asserted the sample contained air.
It now samples full vertical columns and sees 3,581 solid against 2,819 air.

`test-home-island-shape` is written and wired. `src/test_home_island_shape.c` was a
0-byte file; it now asserts eight shape invariants derived from `world_generate_home()`
rather than from captured output — pointed bottom, monotonically widening slices, circular
cross-sections within `island_radius`, the reserved 1-voxel border, no stone above the
midpoint, and centroid on the world axis. Its stone count (16,922 at 64³)
cross-validates against `test-refactored-worlds`.

The gate now runs seven suites: the original three plus `test-home-island-shape`,
`entropy-test`, `universe-entropy-simple-test` and `universe-entropy-test`. Only
self-contained suites were added — `test-load-world` is deliberately excluded because it
depends on a fixture under `worlds/`, which is not checked in.

Still to do:

- A build-only smoke target for the other binaries, so a target breaking is caught by CI
  rather than by hand five months later. All 31 build today, but nothing keeps them building.
- `src/network.c` and `tests/types.js` are still empty.

### 1.4 Decide what blocks a merge

Codecov is currently `continue-on-error` with no token, so coverage cannot fail a build.
Either wire the token and set a floor, or drop the step and stop implying a gate that
does not exist. Given that the 5 JS tests cover the thin wrapper and not the engine, the
honest move is probably to stop treating JS coverage as a release signal and let the
native suites be the gate.

**Exit criteria**

- `native.yaml` green on Linux and macOS from a clean checkout
- `test.yaml` green, or explicitly reduced to what can pass
- Both trigger on the active branch
- Branch protection reflects the gates that genuinely gate

---

## Phase 2 — Correctness and hygiene

Target: 3–5 days. Now that CI proves things, changes here are safe.

### 2.1 Fix the real warnings — **done for the type-safety classes**

Completed:

1. **Struct tags named** — `universe.h` and `synthesizer/synthesizer.h` now use
   `typedef struct Universe {…}` / `typedef struct Synthesizer {…}`, so the
   `struct X;` forward declarations in `title_hum.h`, `world.h` and
   `universe_context.h` name the same type. Cleared 20 warnings.
2. **Anonymous structs across function boundaries** — `isometric_renderer.c` now has a
   named `RenderBatchEntry` shared by `flush_batch_internal`, `add_to_batch_internal`
   and the caller's buffer; `window.c` has a named `HeroSelectionData`. Cleared 4.
3. **`-Wformat`** — all 35 `%lu`-with-`uint64_t` sites replaced with `PRIu64` across
   `cpu_renderer_optimized.c`, `sprite_renderer.c`, `unified_renderer.c`,
   `universe_reference.c`, `universe.c` and `world.c`. Also fixed a real
   `-Wformat-extra-args` bug in `window.c` (three coordinate arguments passed to a
   format string that only consumed the name).

**`-Wincompatible-pointer-types` and `-Wformat` are now both zero.** Total VERSE-owned
warnings went from 332 to 272.

Remaining, and deliberately not done:

4. **`-Wsign-compare`** (44) — all 44 sites were read individually. Every comparison
   against an unsigned dimension is already preceded by a `< 0` guard (see
   `world_transition.c:28/38/166–184`, `world_spawn.c:246`) or is a zero-based loop, so
   none is a live out-of-bounds risk. Worth tidying with `size_t`, not urgent.
5. **~208 `unused-*`** — pure hygiene. Consider `-Wno-unused-parameter` so the
   signal-carrying warnings stop drowning.

Goal: a warning count low enough that a *new* warning is visible. Then consider `-Werror`
for the release build.

### 2.2 Repair `make verse` — **done, and generalised**

The root cause was broader than `verse`: **13 of 31 Makefile targets did not build.**
Ten of them failed the same way — `world.c` had grown dependencies on bulk ops, universe
placement, entropy sampling and constants, but each of ~25 link lines re-listed its
objects by hand, so any line written before those dependencies existed was stale.

Fixed by declaring the closure once, verified closed with `nm -u`:

```make
WORLD_CORE_OBJS = src/world.o src/world_bulk_ops.o src/constants.o \
                  src/universe.o src/entropy_field.o src/universe_coords.o \
                  src/world_entropy_generator.o $(SHA256_OBJ)
```

Every link line now uses it, so a new dependency inside the world core cannot silently
break unrelated targets again. `WORLD_CORE_SRCS` does the same for the Emscripten build,
which was missing four translation units.

The duplicate `world_fill_region` was a false alarm: the competing definitions live in
`world_voxel.c` and `test_generation_module.c`, neither of which is linked by any target.

The other three failures were separate and are also fixed:

- `verse` — `src/server.o` was compiled with `-DMAIN_SERVER`, which gave it a second
  `main()` that collided with `main.o`. That flag exists to build `server.c` as a
  standalone binary and does not belong in the `verse` link.
- `world-viewer` — referenced `VOXEL_METAL_COPPER`/`VOXEL_METAL_SILVER`, which were
  renamed; the refined metals are now `VOXEL_COPPER`/`VOXEL_SILVER`.
- `world-editor-renderer-benchmark` — the Makefile looked for the source at the repo
  root instead of `src/`, and the file itself called a `universe_alloc()` that does not
  exist and passed 6 arguments to a 7-argument `isometric_renderer_add_voxel`.

All 31 targets now build from `make clean`.

### 2.3 Decide the fate of both refactors

This is a judgment call, and leaving it unmade is itself the cost — the monolith and its
replacement both need maintaining, and the duplicate `world_fill_region` above is exactly
the kind of bug that produces.

For the `world.c` decomposition (`world_core.c`, `world_voxel.c`, `world_noise.c`,
`world_serialize.c`, ten `world_generation_*.c`) and for `verse_client_modular` plus the
seven `client_*.c` modules, pick one per refactor:

- **Finish it** — wire the modules into `CLIENT_SOURCES`, delete the monolithic paths,
  let `release-check-native` prove equivalence.
- **Abandon it** — delete the partial modules; keep `world.c` as the single source of
  truth.
- **Park it** — move to a clearly named branch and delete from `master`.

Any of the three is better than the status quo. Decomposing a 7,793-line file is genuinely
worthwhile, but only as deliberate work with the native suites as a safety net — not as
a background refactor competing with a release.

### 2.4 Retire the orphans

61 `.c` files (~15,000 lines) are referenced by no Makefile. They are invisible to CI and
cannot be verified. Delete them (git now remembers), or give them targets. Same for the
literal backups: `world.c.backup`, `window.c.backup`, `fp_renderer.c.bak`,
`cpu_render_phases_test.c.backup`, `scripts/browser copy.js`, `scripts/build copy.js`.

Exception: keep `window_interactive_test.c` — Phase 4 ports from it.

### 2.5 Memory safety pass — **started**

Close out the standing `AGENTS.md` TODO:

- `make sanitize-threads` covers the threading test under TSan and ASan/UBSan, and already
  found two real bugs in `world_generate_with_type` (see Status). Extend the same treatment to
  the other suites; the generator is clearly worth the attention.
- Run `valgrind` (Linux) or `leaks` / ASan+UBSan (macOS) against the remaining native suites
  and a scripted client session.
- Audit the 38 `strcpy`, 22 `sprintf`, and 6 `strcat` sites; convert to bounded variants
  where the length is not provably safe.
- Spot-check the 271 allocation sites for unchecked returns, prioritizing the world
  generation and save/load paths.

Scope this to the release path — `verse-client` and the three suites — rather than all
86k lines.

### 2.6 Documentation cleanup

Move the ~50 superseded root documents into `docs/history/` (or delete them; git
remembers). The eight `DECOMPOSITION_PROGRESS*` files, the `FINAL_*` terrain series, and
the `*_SUMMARY.md` files all describe intermediate states of work that either shipped
long ago or never shipped, and they read as current.

Delete `EXECUTIVE_SUMMARY.md` and `NASA_EVALUATION_REPORT.md`. They are roleplay
artifacts — a fictional author line, a `Date: [Current Date]` placeholder, and a
$300,000/8-month/4-developer program — and someone will eventually mistake them for a
plan of record.

Note that `docs/` is jsdoc's output directory (`npm run make:docs` writes there with
`-d docs`), so hand-written documentation needs a different home — `docs/history/` works
only if jsdoc output moves, otherwise use `notes/` or similar.

Keep and update: `README.md`, `AGENTS.md`, `CHANGELOG.md`, `GAME.md`, `WORLD.md`,
`FABRIC.md`, `DEVELOPERS.md`, `NEWBIE.md`, `reports/memory_report.md`.

Correct `STATUS.md`: mark main menu complete, and annotate character creation, intro, and
chapter as partial rather than absent.

**Exit criteria**

- Warning count materially reduced; no `-Wincompatible-pointer-types`, no `-Wformat`
- `make verse` links; covered by CI
- One decision recorded per refactor
- Sanitizer or valgrind run clean on the release path
- Root holds fewer than ~10 markdown files, all current

---

## Phase 3 — Tag `1.0.0-rc.1`

Target: half a day.

1. `npm run release:verify` passes end to end (now that `scripts/release-verify.sh` is
   committed).
2. Confirm `VERSION` and `package.json` still read `1.0.0-rc.1`.
3. Update `CHANGELOG.md`: keep the existing 2026-03-31 entry, and add a note that the
   release engineering it describes is now committed and CI-verified. That gap is worth
   recording, since the next reader will wonder why a dated changelog entry precedes its
   own commit.
4. Tag `v1.0.0-rc.1`, push tags.
5. Attach macOS and Linux `verse-client` builds from CI to the release.

**Exit criteria** — a third party can clone the tag, run `make release-check-native`, and
get a working `verse-client`. That is the whole point of `rc.1`.

---

## Phase 4 — Playable onboarding

Target: 2–3 weeks. This is the only phase with substantial new development.

Six of eleven screens work; the vertical slice is real. The gap is the guided path from
launch to first gameplay. `src/window_interactive_test.c` already prototypes most of it —
scene JSON loading, a four-page Genesis narrative, quest boxes, character save-on-create.
Prefer porting from it over writing fresh.

### 4.1 Save/load — do this first

The most visible broken promise: "Continue" starts a new game and "Save Game" prints
"not implemented yet."

- Implement `game_state_load_game()` (`game_state.c:220`) against the existing
  `character.c` save/load and `PlayerId` code, which is written but unwired.
- Wire in-game save (`verse_client.c:409`).
- Fix "Continue" (`verse_client.c:467`) to load the most recent save.
- Remove the duplicate main-menu "Continue" button (`window.c:1156` vs `1166`).

Sequenced first because everything after it needs somewhere to persist state, and because
it converts two visible lies into working features.

**Wiring status, measured.** The persistence layer is more complete than the wiring:

| Layer | State |
| --- | --- |
| `settings_save` / `settings_load` | wired, called from `window.c` |
| `character_save_game` / `character_load_game` | implemented, **no caller outside `character.c`** |
| `world_save_by_seed` | one caller (`game_state.c:199`, main-menu world) |
| `world_load_by_seed` | implemented, **never called** |

So character state is never written during play, and worlds are saved but never read back.

**A data-loss bug in the world format is fixed.** `world_load` restores eleven metadata
fields from the deserialised world, but `world_serialize` never wrote any of them, so every
load silently reset `generation_type`, `seed_id`, `rarity`, `vector_clock`, `rng_state`,
`universe_depth`, `history_event_count`, `unique_player_count`, `base_level`, `score` and
`level` to `world_create` defaults. A HOME world round-tripped as `WORLD_TYPE_SCOURED`
with an empty seed — which for a seed-driven procedural game means a saved world could not
be regenerated or extended consistently.

Fixed by appending a `META1` block after the voxel payload. Placing it there keeps both
directions compatible: readers that stop at `voxel_count` ignore it, and saves written
before it existed still load and simply keep the defaults. Verified in both directions —
a pre-existing save still loads, and a fresh save now round-trips `generation_type` 0
(HOME) with its seed intact.

**Voxel state loss is now fixed too, and it was worse than "conditions".** The legacy
per-voxel stream stores one hex character per voxel, which is `type & 0x0F` — only the low
four bits. With `VOXEL_COUNT` at 139, every type from 16 upward was silently aliased to a
different type on load: measured at 4.1% of a wilderness world and 6.2% of a farm. Ore,
crystal and the named stone types were all in that range, so saving and reloading a world
quietly replaced them. `condition_mask` and `data8` (entropy, quantity, damage,
temperature, heat) were not stored at all.

A `VOX2` section now carries the real values: run-length encoded full types, plus sparse
index lists for `condition_mask` and `data8`. RLE suits the data — runs are 0.3%–2.4% of
voxel count — and the two sparse fields are almost always zero outside gameplay, so the
whole thing costs about 13% more file size. The legacy stream is still written byte for
byte, because `assets/viewer.html` and older builds read it.

`src/test_world_roundtrip.c` (in `release-check-native`) pins this down: it plants a type
above 16, condition bits at both ends of the 64-bit mask, and all eight packed fields, then
asserts every one of 32,768 voxel types survives, that nothing spurious appears, and that a
save with the `VOX2` section truncated away still loads.

One fidelity gap remains and should be decided explicitly:

- **Universe coordinates are not persisted.** `world_save_by_seed` contains a block that
  reads as if it saves them but assigns `world->universe_x = world->universe_x`.

### 4.2 Character creation

Extend the existing name-entry modal into real creation: the six GAME.md attributes
(STR/DEX/INT/WIS/CON/LUCK), and a `character_save_game()` call on confirm so a character
exists as a record rather than a `player_name` string.

### 4.3 Genesis introduction and Chapter 1

Load `scenes/00000-genesis.json` and `chapters/00000-genesis.json` instead of the
hardcoded placeholder. Port `load_scene()` and `get_chapter_content()` from
`window_interactive_test.c`. Implement `game_state_show_chapter()` and
`game_state_advance_chapter()`, which are declared in `game_state.h:185-186` but have no
definitions.

### 4.4 Tutorial quest

Spawn the player in the Garden rather than the wilderness (`game_state.c:960-962`). Load
`quests/00000-humble-beginnings.json`, call `window_init_tutorial_quest()`, and render the
tutorial modal. Implement the GAME.md tutorial loop: kill five Spirit Raider Sprites, mobs
at five minutes, failure at ten.

This needs a minimal quest engine — objective tracking, completion, timers. Keep it
narrow; a general quest system is not required to ship one tutorial.

### 4.5 Decide on player registration

Currently server-side only: `proxy.c:1142-1203` handles `CREATE_CHARACTER` over
WebSocket, with no client UI. Genuine scope decision — see below.

### 4.6 Playtest

The whole path, repeatedly, on both platforms. Watch generation time on the 54-world
async load, memory against `reports/memory_report.md` (~1.5 MiB per 32³ world, ~40.5 MiB
per 27-world cluster), and save/load round-trips.

**Exit criteria** — a new player launches the client, creates a character, watches the
intro, completes the tutorial, saves, quits, and resumes. Every `STATUS.md` box checked
truthfully.

---

## Phase 5 — Ship `1.0.0`

1. Full `npm run release:verify` plus `npm test` locally, including the Puppeteer suite.
2. Complete playthrough on macOS and Linux from clean clones.
3. Sanitizer run over the full onboarding path, not just the native suites.
4. `STATUS.md` fully checked; `CHANGELOG.md` written for `1.0.0`.
5. Bump `VERSION` and `package.json` to `1.0.0`; tag; attach builds.

---

## Decisions needed

These shape the plan and are yours to make. The first three affect what gets committed;
decision 1 blocks a buildable first commit.

1. **`src/noise-c` — blocks the first commit.** Required by the build, but it is a nested
   git repo, so it is currently excluded by `.gitignore` and **a fresh clone will not
   build**. It is an unmodified clone of `https://github.com/rweather/noise-c.git` at
   `cfe25410`, which is on that remote, so nothing original is at risk. Recommend
   registering it as a submodule and setting `submodules: recursive` on
   `actions/checkout` in both workflows; alternatively remove its `.git` and vendor the
   source (~29 MB). The same question applies to `android/third_party/{SDL,SDL_ttf,
   freetype,jansson}`, four more nested clones, currently ignored.

2. **`assets/pokemondb.net/` (332 MB, third-party scraped sprites).** Recommend excluding
   from git and sourcing sprites deliberately — it is both too large and a licensing
   exposure in an MIT-licensed public repository. Currently excluded. Confirm, or say
   where it should live.

3. **`models/` (~100 MB, 1,798 files).** Now staged, and the largest thing in the index.
   Some files are genuinely referenced by `world_viewer.c`, `world_tool.c`, and the
   importers, but most is bulk third-party model packs — the same licensing and size
   question as `pokemondb.net`. The `.zip` originals and `models/.archive/` are already
   excluded. Worth trimming to referenced assets before committing.

4. **Where the work lands.** All of it is on `feature/a-frame` while `master` holds the
   2024 JavaScript project and both workflows target `master`/`main`. Options: make
   `feature/a-frame` the new `master`; merge into `master`; or start a fresh default
   branch. Affects branch protection and CI triggers.

5. **Player registration in `1.0.0`?** Only a server-side WebSocket path exists. Cutting
   it makes `1.0.0` a single-player release with multiplayer to follow — a defensible
   simplification given that the tutorial and save/load matter more to a first-time
   player.

6. **The two refactors** (Phase 2.3). Finish, abandon, or park — one decision each.

7. **A-Frame and the web client.** `aframe` is a dependency and the branch is named for
   it, but the native SDL2 client is what ships. If the web client is not part of
   `1.0.0`, dropping `aframe` removes the `three-bmfont-text` git+ssh dependency and
   simplifies Phase 1.2.

8. **`rc.1` scope.** This plan tags `rc.1` at engine-and-tooling per the existing
   CHANGELOG. If you would rather tag only once, Phases 3 and 5 collapse — but then
   nothing is verifiable until Phase 4 completes, which is a long time to go without a
   checkpoint.

---

## Sequencing

```
Phase 0  Preserve            one sitting    ← gate; do not defer
Phase 1  Honest CI           1–2 days
Phase 2  Correctness         3–5 days
Phase 3  Tag rc.1            half a day
Phase 4  Onboarding          2–3 weeks
Phase 5  Tag 1.0.0           1–2 days
```

Roughly two weeks to `rc.1` and six to eight to `1.0.0` at a steady solo pace. Phases
0–3 are recovery and cleanup of work already done; Phase 4 is the only substantial new
development.

The dependencies that matter: Phase 0 gates everything, because nothing can be verified
until a clean checkout builds. Phase 2.1's struct-tag fix needed to precede Phase 1.1,
since GCC on Linux is stricter about those mismatches than clang — that is now done, so
1.1 is unblocked. Phase 4.1 (save/load) should precede the rest of Phase 4.

---

## Status

**Phase 0** — `.gitignore` written; 2,384 files (~102 MB) staged, including all
`CLIENT_SOURCES`. No first-party source untracked. Remaining: resolve decision 1
(`src/noise-c`), review the staged set, commit in slices, push.

**Phase 1.3** — done apart from the build-only smoke target. The gate now runs ten
suites and, more importantly, can now fail.

**Phase 2.1 / 2.2** — done. All 31 Makefile targets build from `make clean`;
`-Wincompatible-pointer-types` and `-Wformat` are both zero; VERSE-owned warnings 332 → 272,
with the remainder confirmed to be hygiene rather than latent bugs.

**Phase 4.1** — save/load now round-trips losslessly. The metadata block fixed the
eleven-field reset; the `VOX2` section fixed voxel state loss, which turned out to include
silent type aliasing for every type above 16, not just the missing condition bits.
`test-world-roundtrip` guards it. The feature work itself is untouched.

**World generation performance** — a new-game sequence generates 54 worlds of 128³ and took
about six minutes. It is now about 3.4 minutes, with generated worlds bit-identical
(verified by hashing all 2M voxels; original and optimized builds both yield
`287f3b72b2f01364`). Three defects, all the same shape — an expensive pure sample taken
before the cheap test that discards it:

- `sample_occupancy_variation` ran once per voxel and the result was never read. clang had
  been reporting `unused variable 'occupancy_var'` all along, so one of the warnings
  dismissed as cosmetic in Phase 2.1 was in fact 18% of generation time.
- The ore path sampled a noise channel and then let the next line's depth test discard it,
  seventeen times per voxel. All seventeen share one probability formula, so they collapsed
  into a table whose depth band gates the sample: ~8 channels per candidate instead of 17.
- The crystal path took two expensive samples before a `depth_ratio > 0.8` test that
  rejects four voxels in five. Hoisting the test cut that phase by 95%.

The per-voxel `gettimeofday` instrumentation is gone, replaced by `src/world_gen_profile.c`
behind `-DWORLD_GEN_PROFILE` and a `world-generation-profile` target, so shipping builds
carry no timing cost. The old instrumentation cost about 1.7% (Darwin's `gettimeofday` is a
13.5 ns comm-page read, not a syscall) but was also useless: nested timers divided by a
running average always reported "Terrain Generation: 100%".

The remaining opportunity is architectural, not micro: 26 of the 54 worlds are a wilderness
ring the player does not start in, and they cost 195 of the 205 seconds. Deferring them
would take a new game to roughly 10 seconds without touching the generator. See the
`world-generation-performance` canvas for the full breakdown.

**Threading** — world generation now runs on a worker thread instead of on the main thread's
step machine, so the loading screen animates while worlds are built. Three new files:

- `src/task_scheduler.{h,c}` — a pthread worker pool with a task queue and task groups. It is
  deliberately coarse: one task per world, not a parallel-for. If the pool was never started,
  submitted tasks run inline before `task_group_submit` returns, which is what lets the
  headless tools and tests use the same call sites without starting threads.
- `src/world_gen_job.{h,c}` — the generation sequence as one background job, publishing
  progress under a mutex for the loading screen to read. The step sequence is preserved
  exactly, including two pre-existing quirks (`adjacent_farm_worlds[0]` is never filled, and
  the centre of the wilderness ring is skipped).
- `src/world_physics_jobs.{h,c}` — steps one world or a group of worlds concurrently, one task
  per world. This is the "physics for a group of worlds" case. `game_state_step_world_physics`
  calls it for the current world and its loaded neighbours, gated on the runtime clock; see the
  cost measurement below for why it is gated rather than free-running.

The main thread does not touch the worlds or the universe while generation runs:
`game_state_update` returns early while `world_generation_active`, the loading screen renders
no world, and `current_world` is only assigned once the job reports complete. `make
sanitize-threads` builds the threading test under ThreadSanitizer and under ASan/UBSan and
repeats each 20 times (`REPS=` to change), because a scheduling bug rarely shows on the first
run; both are clean.

**Two memory bugs in the generator, found by that ASan/UBSan target.** Both predate this work
and neither is a threading bug, but the first one had to be fixed before generation could move
to a worker at all:

- `world_generate_with_type` derived `seed_id` and `rarity` from a double SHA256, but passed the
  first 32-byte digest to `calculate_sha256`, which measures its argument with `strlen`. That
  read past the end of the stack buffer and stopped at whatever zero byte turned up, so both
  values depended on the bytes sitting behind the buffer rather than on the seed — and a
  worker's stack does not hold what the main thread's does. Fixed with a length-taking
  `calculate_sha256_bytes`. Note this changes `seed_id` and `rarity`, and therefore terrain, for
  every seed: the previous values were an artefact of stack layout, not a specification, so the
  generation hash recorded in the `world-generation-performance` canvas no longer reproduces.
  `test-task-scheduler` now asserts that a world generated on a worker is identical to the same
  seed generated on the main thread, which is the property that was silently false before.
- `generate_gravity_from_seed` built a `uint32_t` with `hash[0] << 24`. `uint8_t` promotes to
  `int`, so any byte above 127 overflowed a signed int, which is undefined rather than the
  wraparound the expression assumed. Fixed by casting each byte before shifting.

`world_update_springs` is excluded from the parallel path on purpose. It holds a function-local
`static uint64_t last_water_generation`, so running it for several worlds at once both races on
that value and lets one world's springs suppress another's. `world_physics_step_springs` keeps
it on the calling thread until that static becomes per-world state.

**Fluid simulation is nondeterministic by design** — found while trying to assert that threaded
physics matches serial physics. `world_step_fluids` ends its lateral fill on
`now_ms() - t0 > 8ULL`, a wall-clock budget, so how much fluid moves in one call depends on how
fast the machine is at that instant. Two identical worlds stepped identically diverge, with no
threads involved: reproduced 3 times in 20 under ThreadSanitizer's slowdown, never in 60 runs
of the normal build. This is not a threading bug and predates this work, but it matters beyond
the test: it means fluid state cannot be reproduced from a seed, which has consequences for
multiplayer agreement and for replaying a save. `test-task-scheduler` therefore asserts
isolation invariants — canary worlds that must not change, every world stepped, no world
losing fluid — rather than comparing snapshots.

**A fluid step costs ~90ms per world whether or not the world holds any fluid**, which is the
finding that shaped how the physics tick is wired. Measured at 128³ with `-O2`:

| World | One `world_step_fluids` |
| --- | --- |
| Empty, all air | 93 ms |
| Solid lower half, still no fluid | 95 ms |
| Generated home island (no water) | 117 ms, then 71 ms |

Since an empty world costs the same as a full one, essentially none of that time is simulation.
`world_step_fluids` sweeps the whole volume looking for magma through `world_get_voxel` per
voxel, `calloc`s three arrays sized to the world, and walks every column to find its surface —
all before it knows whether there is anything to move. The `max_cells` budget does not bound any
of it.

Threading does not rescue this. Eight worlds stepped across the pool took 805–969 ms against
100–275 ms for one, about 1.7–2.1x for 8 tasks: at roughly 100MB of voxels per world the work is
bound by memory bandwidth, not cores.

So `game_state_step_world_physics` runs on the runtime clock — the same switch the world editor
simulates under, already toggleable in the client — at one tick per second, and a new game is
unaffected because the clock starts stopped. That is deliberately a way to space out an
unavoidable hitch, not a simulation rate. **A frequent tick needs `world_step_fluids` fixed
first**, and the fix is well-defined: track fluid cells (or at least a per-world "has fluid"
flag maintained by `world_set_voxel`) so an empty world costs nothing, and index `world->voxels`
directly instead of calling `world_get_voxel` two million times. Worth noting the world editor
calls this every frame with a full-volume budget, so it is paying the same 90ms whenever its
clock runs.

**The home island had no features at all, and the tree code could never have run.** The generator
built the island as a solid stone paraboloid and then, in its only decoration pass, planted trees
only where `top_voxel->type == VOXEL_GRASS`. Nothing ever created grass — the comment above it
said "just generate the stone shape without grass/dirt layers" — so the condition was never true
and not one tree was ever placed. Measured on a generated island before the fix: 149,055 voxels,
every one of them `VOXEL_STONE`, and 0 of 11,681 surface columns grass-capped. The island was a
featureless grey plate with a perfectly flat top, which is why standing on it read as being
*inside* it: no relief, no ground cover, and nothing on the surface to judge depth against.

`world_generate_home` now keeps that stone core exactly as it was and adds three things over it:

- Relief cut *down* from the flat top using two octaves of hash-based value noise, so the
  silhouette, the reserved 1-voxel border and the pointed bottom all survive untouched and the
  centre stays the island's highest ground. The surface spans 6 levels instead of 1.
- Soil and grass layered over the stone, which is what makes the surface plantable at all.
- Boulders, bushes and tree copses. Boulders drape over the terrain — each column of the dome is
  measured from its own surface height, not the centre's — because a flat slab cut into sloping
  ground and buried the neighbouring soil band under rock.

Two things the shape test caught that would have been easy to miss. Decorations are placed by
their centre but occupy a footprint around it, so gating them on the arena radius alone let
boulders just outside the arena overhang into it — precisely what gravity would then drop the
player onto; the keep-out radius now exceeds the arena by the largest footprint. And the boulder
density that looked right at 128³ swamped a 64³ island, which is the size the test builds.

The seed now reaches the island. It never did before: `world_generate_home` opened with
`(void)seed`, and since no decoration was ever placed, nothing downstream consumed the seeded RNG
either — every home world in every universe was byte-identical. Three seeds now give three
different islands. Generation costs 80ms at 128³, against roughly 10s for a new game.

There are still no fluids on the home island, which is what was wanted, and the test now asserts
it rather than assuming it: no water, magma, steam, oil or gas, and no spring of any kind, since a
spring would flood the island on the first physics tick even though the spring voxel is solid.

`test-home-island-shape` was rewritten around this construction. Its shape invariants previously
counted `VOXEL_STONE` specifically, so capping the core with soil and grass broke two of them; they
now apply to terrain, with separate assertions for the surface treatment, the features, the
absence of fluids, the spawn, and that a seed rebuilds the same island. It also needed
`world_spawn.o` linked, since it now exercises the spawn. Worth noting `voxel.h` declares
`voxel_type_is_fluid()` but nothing anywhere defines it.

**Spawn and landing** — the player now spawns one voxel clear of the island surface and falls
to it. `game_state_check_falling` teleported the player down a voxel per frame and updated only
the integer `player_z`, leaving `player_world_z` behind; it is replaced by
`game_state_apply_gravity`, which integrates `velocity_z` against `world->gravity`, converts
m/s² to voxels using `VOXEL_METERS_PER_SIDE` the same way `world_step_actors` does, steps down a
voxel at a time so a fast fall cannot pass through a floor, and comes to rest at the voxel
centre. `test-spawn-gravity` drives the real `GameState` against a generated home island and
checks all of it, including that gravity never moves the player upward.

`world_get_home_spawn_position` used to scan one column and trust it — the column it picked was
also off the island's axis, because `world_get_center_position` derives a "center_z" from the
world's *depth* and callers use it as a horizontal coordinate. Now that the surface has relief
that column is not necessarily the highest ground, so the spawn instead searches for the highest
open-ground column and takes the one nearest the axis. Open ground excludes boulders and canopies,
so the player cannot be set down on top of a tree. With the arena flat and at the island's maximum
height, that resolves to the centre, and the drop is one voxel. The latent confusion in
`world_get_center_position` is left alone here; it still affects `world_is_center_tile`, which is
called with a y value where a depth-derived coordinate is expected, but only the cosmetic
`is_center_tile` flag depends on it.

Verified after these changes: `make all` and every Makefile target builds, the ten suites in
`make release-check-native` pass, and no file that was edited gained a warning.

The base-system integrity question is answered: the engine's world, universe, entropy,
generation and persistence layers are sound, and every defect found was in the scaffolding
around them — link lines, test harnesses, stale call sites, and a serializer that had
fallen behind its own loader. Phase 1.1 (`native.yaml` from a clean checkout) is the
next real gate, and it is blocked only on decision 1.
