## Render performance optimizations (moved)

This document has been unified into `OPTIMIZATION.md`.

Highlights now included in the canonical plan:
 - Chunk AABB culling and per-slice tile clamping
 - Occupancy-bitfield face culling
 - GPU batching via SDL_RenderGeometry and persistent buffers
 - Greedy meshing, hierarchical culling, LOD, caching, and parallelization

Please see `OPTIMIZATION.md` for the complete and current plan.

### Already implemented
- **Chunk AABB culling**: coarse screen-space culling per chunk in `isometric_renderer_render_world`.
- **On-screen tile clamping (per z-slice)**: inner (x,y) loops clamped to the viewport using u=x−y and v=x+y bounds in `isometric_renderer.c`.
- **Occupancy-bitfield face culling**: when `world->occupancy_bits` matches dimensions, faces hidden by solid neighbors are skipped.
- **GPU batching path**: uses `SDL_RenderGeometry` and a persistent vertex buffer.

### Recommended next steps

#### 1) Greedy meshing per chunk (big win)
- **Goal**: Replace per-voxel draw with merged quads or triangle lists per chunk.
- **Approach**:
  - Partition worlds into meshing chunks (e.g., 32×32×Z or 16×16×16).
  - For each chunk, build meshes for faces that are visible (top/left/right only for the isometric view).
  - Use greedy rectangle merging across runs of identical material/color to minimize quads.
  - Store results in `IsoChunkMesh` objects kept on the GPU (reuse persistent buffers, grow geometrically).
  - Rebuild meshes only when chunk becomes “dirty” (voxel edits or neighbor changes).
- **Sketch**:
  - Data
    - `struct IsoChunkMesh { SDL_Vertex* vertices; int vertex_count; int material_id; uint32_t version; }`
  - API
    - `void iso_build_chunk_mesh(World* w, Rect3i chunk_bounds, IsoChunkMesh* out);`
    - `void iso_draw_chunk_mesh(const IsoChunkMesh* m);`

#### 2) Hierarchical culling and range limiting
- **View distance clamp** in world-space: only consider chunks whose centers fall within a diamond radius around the camera.
- **Heightmap precompute**: `height[z_top(x,y)]` per (x,y) to quickly skip empty columns and render only top faces for far LOD.
- **Empty-space skipping**: maintain per-chunk occupancy summary (e.g., bit if any solid in z-bands) for rapid z-window rejection.

#### 3) Level of detail (LOD)
- **Near ring**: full meshing.
- **Mid ring**: coarser greedy meshing or decimated sampling (e.g., 2×2 columns collapsed).
- **Far ring**: heightmap-only (top faces), optionally downsampled in (x,y).
- **Screen-space rule**: pick LOD by projected tile size; if tile width < N pixels, step up LOD level.

#### 4) Caching and reuse
- **Per-column base color** cache (including wetness tint), invalidated on edits or layer texture changes.
- **Visibility masks** cached per chunk for current `camera_z`; invalidate when camera_z changes or chunk edits occur.
- **Camera-stable reuse**: reuse previous frame’s visibility/mask when the camera moves < 0.5 tile.

#### 5) Parallelize heavy work
- **Background meshing jobs**: thread pool that builds/updates `IsoChunkMesh` objects; main thread only submits geometry.
- **Visibility and culling**: compute chunk-visibility and z-bounds in parallel per z-band.

#### 6) Data layout and memory
- **Linearize hot loops**: ensure Z is the innermost dimension in memory for better cache with vertical scans.
- **Compact voxel**: use `uint8_t` (or small enum) for `VoxelType`; avoid padding in `Voxel`.
- **Occupancy bitfield generation**: on load, populate `world->occupancy_bits` if absent; update incrementally on edits.

#### 7) Batching and materials
- **Material buckets**: group triangles by material/color to minimize state changes (if/when textures are introduced).
- **Stable sort key**: keep painter order parity (y, then x, then z) within material groups to preserve visuals.

#### 8) Resolution scaling
- **Dynamic resolution**: when frame time rises above a threshold, render into a smaller offscreen target (e.g., 0.75× or 0.5×) and upsample.
- **UI preserving**: composite world layer under native-resolution UI.

#### 9) Instrumentation and diagnostics
- **Frame stats**: counters for voxels processed, culled, triangles submitted; per-stage timings.
- **Overlay**: toggleable F3-like readout; optional chunk bound visualization.

#### 10) Editor/runtime toggles
- Config or hotkeys to enable/disable: greedy mesh, LOD, chunk outlines, wireframe, heightmap-only far ring, dynamic resolution.

### File touchpoints
- `src/isometric_renderer.c`: culling, meshing submission, GPU batching, LOD selection.
- `src/world.c` / `src/world.h`: occupancy bitfield, optional `heightmap` attachment and maintenance.
- New: `src/iso_mesh/` (suggested) for meshing code and worker job system.

### Minimal staged plan
1) Add heightmap + occupancy summaries; clamp render by heightmap for far tiles.
2) Introduce chunk meshes (top-only first), update on edit; draw via GPU path.
3) Add LOD ring selection and dynamic resolution fallback.
4) Parallelize mesh builds; instrument and tune.


