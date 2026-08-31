## Voxel Engine Performance Plan (Unified)

This document is the canonical, unified plan for maximizing performance in the endless, edge-connected voxel world. It merges and supersedes prior notes; see `OPTIMIZATIONS.md` for a brief redirect.

Goals: unify the isometric and first-person renderers around shared primitives and data, minimize CPU work per-frame, and exploit GPU batching.

### Objectives
- Achieve O(visible) rather than O(total) frame cost using visibility culling and greedy meshing.
- Share low-level hot-path helpers across renderers to avoid divergence (`src/voxel_render_common.h`).
- Move high-frequency checks to bitfields and precomputed tables.
- Batch draw calls and prefer GPU-friendly paths (SDL_RenderGeometry / indexed buffers) where available.

### Data Layout & Fast Access
- Keep `World` voxel memory contiguous in X-major order and keep `world_linear_index_fast` inline (already in `world.h`).
- Maintain an optional CPU occupancy bitfield (`World::occupancy_bits`) mirroring non-air voxels for fast neighbor checks and visibility decisions. Ensure it is kept in sync:
  - Rebuild once after generation/import, then apply incremental updates on `world_set_voxel` or editor brushes in chunks.
  - Provide `world_refresh_occupancy_bitfield()` (already declared) and call it after bulk edits.
- Use simple, branch-light inline helpers for bounds, linear index, and transparency classification. Shared in `voxel_render_common.h`.

### Visibility & Culling
- First-person (FP): keep DDA ray traversal (already in `world_raycast_first_hit`).
  - Consider hierarchical stepping (jump to next solid column using height field caches) for vertical shots.
  - Add near/far planes to bound max steps based on FOV and panel size.
- Isometric (ISO): use orthographic visibility approximations when camera is near `(1,1,-1)` with up `(0,0,1)` (already in `world.c` fast path).
  - Integrate `world_visible_count_for_viewport` as a guide for LOD and dynamic resolution scaling.
  - Slice-aware rendering: leverage camera Z to skip faces occluded above slice.
  - Coarse chunk AABB culling and on-screen tile clamping per z-slice using u=x−y, v=x+y bounds (present in `isometric_renderer.c`).
- Both: perform frustum/viewport culling against world AABBs and chunk AABBs before per-voxel work; extend ISO’s scaffolding to full 3D AABB-vs-viewport checks.

### Meshing & Batching
- Implement greedy meshing per-chunk in 3 passes:
  1) Top faces (dominant visual in ISO) – merge rectangles along X and Y where neighbor above is transparent.
  2) Side faces – merge long spans for cliff/walls in ±X/±Y directions.
  3) Bottom faces (optional; usually culled).
- Maintain per-chunk mesh caches keyed by `(world_id, chunk_x, chunk_y, z_slice)` or full `(x0,y0,z0)` region and an invalidation stamp. Only rebuild when voxels in the chunk change.
- Use shared face-exposure check from `voxel_render_common.h` to unify logic across renderers.
- Prefer indexed geometry and triangle lists. Use `SDL_RenderGeometry` when available, otherwise fall back to CPU raster or immediate-mode quads.

Current status (already implemented):
- Chunk AABB culling (coarse) in `isometric_renderer_render_world`.
- On-screen tile clamping per z-slice via lattice-space bounds in `isometric_renderer.c`.
- Occupancy-bitfield face culling when `world->occupancy_bits` matches dimensions.
- GPU batching path using a persistent vertex buffer (SDL_RenderGeometry).

### Chunking & Streaming
- Standardize chunk size of 16x16x16 (configurable) for both generation and rendering paths.
- Maintain an LRU of resident chunks around the camera; stream-in/out from disk or procedural generation threads.
- Keep a small ringbuffer of updated chunks each frame to amortize rebuild cost.
- Precompute per-chunk summaries: occupancy ratio, tallest solid, material histograms; use for LOD and quick reject.

### Level of Detail (LOD)
- Far distance: billboard top-surfaces for ISO (heightmap by dominant type), and coarser voxel stepping for FP (skip N voxels per step with conservative miss test).
- Mid distance: greedy meshed surfaces only.
- Near distance: full per-voxel shading with face culling.

### GPU Strategy
- Single, persistent vertex buffers reused across frames; grow capacity geometrically.
- One texture atlas for per-type colors/overrides; bind once per frame.
- Minimize texture changes; encode per-vertex color as packed RGBA.
- If shaders are available, push fog and simple normal-based AO to GPU.

### Resolution Scaling
- Dynamic resolution: when frame time exceeds a threshold, render into a smaller offscreen target (e.g., 0.75× or 0.5×) and upsample.
- Preserve native-resolution UI by compositing the world layer under UI.

### Caching & Reuse
- Per-column base color cache (including wetness tint), invalidated on edits or layer texture changes.
- Visibility masks cached per chunk for current `camera_z`; invalidate when `camera_z` changes or chunk edits occur.
- Camera-stable reuse: reuse previous frame’s visibility/mask when camera movement is < 0.5 tile.

### Parallelization
- Background meshing jobs: thread pool that builds/updates `IsoChunkMesh` objects; main thread only submits geometry.
- Visibility and culling: compute chunk-visibility and z-bounds in parallel per z-band.

### Editor/Runtime Integration
- After bulk edits: mark touched chunks dirty; enqueue mesh rebuilds.
- For brushes: update occupancy bits incrementally and invalidate only affected chunk(s).
- Use `world_height_at_fast` for quick ground queries and overlays.

### Profiling & Diagnostics
- Add per-frame counters: voxels processed, culled, triangles submitted; per-stage timings.
- Include optional overlay to color by chunk mesh age and draw distance bands; optional chunk bound visualization.

### Editor/Runtime Toggles
- Config or hotkeys to enable/disable: greedy mesh, LOD, chunk outlines, wireframe, heightmap-only far ring, dynamic resolution.

### Immediate Action Items (Implemented/Next)
- Implemented:
  - Shared `voxel_render_common.h` with `voxel_face_exposed_fast` and common transparency rule.
  - Isometric renderer now calls the shared face exposure helper; retains slice-aware top-face rule.
  - FP renderer uses the shared helper for a cheap AO-like shading term without extra branches.
  - Coarse chunk culling, tile clamping, GPU vertex buffering in ISO.
- Next steps:
  - Extract chunking constants to a single header; unify across renderers.
  - Introduce a `ChunkMesh` cache with invalidation and population from greedy meshing.
  - Add AABB-based frustum culling for chunks in both renderers.
  - Migrate isometric GPU path to persistent indexed, material-bucketed buffers with a single submit per world (or per material group).
  - Threaded chunk mesh generation and streaming around the camera.

### Long-Term

### File Touchpoints
- `src/isometric_renderer.c`: culling, meshing submission, GPU batching, LOD selection.
- `src/world.c` / `src/world.h`: occupancy bitfield, optional heightmap attachment and maintenance, visibility APIs.
- `src/fp_renderer.c`: DDA ray casting, dynamic resolution, simple AO.
- New: suggested `src/iso_mesh/` for meshing code and worker job system.

### Minimal Staged Plan
1) Add heightmap + occupancy summaries; clamp render by heightmap for far tiles.
2) Introduce chunk meshes (top-only first), update on edit; draw via GPU path.
3) Add LOD ring selection and dynamic resolution fallback.
4) Parallelize mesh builds; instrument and tune.
- Optional compute or geometry shaders for marching cubes or voxel cone tracing variants.
- Texture streaming for per-type layer textures and large terrain color maps.
- SIMD acceleration for DDA and meshing inner loops.


