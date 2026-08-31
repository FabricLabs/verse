#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "../src/world.h"

static double to_mib(uint64_t bytes) { return (double)bytes / (1024.0 * 1024.0); }
static double to_gib(uint64_t bytes) { return (double)bytes / (1024.0 * 1024.0 * 1024.0); }

static uint64_t per_world_bytes(uint32_t n) {
  uint64_t voxels = (uint64_t)n * (uint64_t)n * (uint64_t)n;
  uint64_t voxel_bytes = voxels * (uint64_t)sizeof(Voxel);
  uint64_t maps_bytes = voxels * sizeof(uint32_t) * 2ULL; // bloom + decaying
  uint64_t world_overhead = (uint64_t)sizeof(World);
  return world_overhead + voxel_bytes + maps_bytes;
}

int main(void) {
  const uint64_t limit = 12ULL * 1024ULL * 1024ULL * 1024ULL; // 12 GiB

  printf("sizeof(Voxel) = %zu bytes\n", sizeof(Voxel));
  printf("sizeof(World) = %zu bytes\n", sizeof(World));

  // Theoretical baseline for 1-voxel world with custom assumptions
  {
    uint64_t per_world = 500ULL; // 200 B/voxel + 300 B overhead
    uint64_t per_cluster = per_world * 27ULL;
    printf("\nTheoretical 1-voxel world baseline:\n");
    printf("Per world: %" PRIu64 " B\n", per_world);
    printf("Per cluster (27 worlds): %" PRIu64 " B\n", per_cluster);
    uint64_t clusters = 1;
    while (per_cluster * clusters <= limit) {
      double mib = to_mib(per_cluster * clusters);
      double gib = to_gib(per_cluster * clusters);
      printf("x%-4llu: %12.2f MiB (%6.3f GiB)\n", (unsigned long long)clusters, mib, gib);
      clusters *= 2ULL;
      if (clusters == 0) break; // overflow guard
    }
  }

  printf("\nActual memory with current structs (includes two epoch maps):\n");
  for (uint32_t n = 32; n <= 4096; n *= 2) {
    uint64_t world_b = per_world_bytes(n);
    uint64_t cluster_b = world_b * 27ULL;
    printf("World %ux%ux%u:\n", n, n, n);
    printf("  Per world:  %12.2f MiB (%6.3f GiB) [%" PRIu64 " bytes]\n", to_mib(world_b), to_gib(world_b), world_b);
    printf("  Per cluster:%12.2f MiB (%6.3f GiB) [%" PRIu64 " bytes]\n", to_mib(cluster_b), to_gib(cluster_b), cluster_b);
  }

  printf("\nDoubling clusters until exceeding 12 GiB (starting from one cluster of 27 worlds):\n");
  // Use 32^3 baseline cluster and double up; then 64^3 etc. can be inferred similarly
  uint64_t cluster32 = per_world_bytes(32) * 27ULL;
  uint64_t clusters = 1;
  while (cluster32 * clusters <= limit) {
    double mib = to_mib(cluster32 * clusters);
    double gib = to_gib(cluster32 * clusters);
    printf("32^3 x %-4llu clusters: %12.2f MiB (%6.3f GiB)\n", (unsigned long long)clusters, mib, gib);
    clusters *= 2ULL;
    if (clusters == 0) break;
  }

  return 0;
}


