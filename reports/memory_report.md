Memory expectations per 32×32×32 world (current)

- World struct: ~200 bytes (pointers and metadata)
- Voxel array: 32×32×32 = 32,768 voxels
  - Voxel = 40 bytes (type + condition_mask + data8 + rotation[3] + momentum[3])
  - Total ≈ 32,768 × 40 = 1,310,720 bytes ≈ 1.25 MiB
- Epoch maps (runtime-only): two uint32_t arrays (bloom/decay) per voxel
  - 2 × 32,768 × 4 bytes = 262,144 bytes ≈ 256 KiB
- Log buffer: small (hundreds of bytes initially), grows with history

Approximate total per world (fresh, in-memory): ~1.5 MiB

Single auto-generated cluster (3×3×3 = 27 worlds)

- Per cluster: 27 × 1.5 MiB = 40.5 MiB

Doubling clusters until exceeding 12 GiB (12 GiB = 12,288 MiB)

- ×1 cluster:   40.5 MiB   (≈0.0396 GiB)
- ×2 clusters:  81.0 MiB   (≈0.0791 GiB)
- ×4 clusters:  162.0 MiB  (≈0.158 GiB)
- ×8 clusters:  324.0 MiB  (≈0.317 GiB)
- ×16 clusters: 648.0 MiB  (≈0.633 GiB)
- ×32 clusters: 1,296.0 MiB (≈1.266 GiB)
- ×64 clusters: 2,592.0 MiB (≈2.533 GiB)
- ×128 clusters: 5,184.0 MiB (≈5.063 GiB)
- ×256 clusters: 10,368.0 MiB (≈10.125 GiB)  ← largest under 12 GiB
- ×512 clusters: 20,736.0 MiB (≈20.25 GiB)    ← exceeds 12 GiB

Largest power-of-two multiple under 12 GiB: 256 clusters (6,912 worlds) ≈ 10.125 GiB


Minimum theoretical baseline (1‑voxel worlds)

- Assume per‑voxel 200 B and per‑world overhead 300 B → 500 B/world
- One cluster (27 worlds): 27 × 500 B = 13,500 B
- Doubling clusters: 27,000 B → 54,000 B → 108,000 B → 216,000 B → … (continues ×2)


Cluster capacity by world size (27 worlds loaded around the player)

Assumptions: 48 B/voxel (40 B voxel + two 4 B epoch maps), ~200 B/world overhead.

- 32×32×32: 27 × (32,768 × 48 B) ≈ 40.5 MiB
- 64×64×64: 27 × (262,144 × 48 B) ≈ 324.0 MiB
- 128×128×128: 27 × (2,097,152 × 48 B) ≈ 2.53 GiB
- 256×256×256: 27 × (16,777,216 × 48 B) ≈ 20.25 GiB   ← exceeds 12 GiB
- 192×192×192: 27 × (7,077,888 × 48 B) ≈ 9.1 GiB      ← comfortably under 12 GiB

Therefore, with the updated voxel (rotation + momentum + entropy) and epoch maps, the largest power‑of‑two world size that supports a full visible 3×3×3 cluster under 12 GiB is 192³ (nearest non-power-of-two) or 128³ for power‑of‑two.


