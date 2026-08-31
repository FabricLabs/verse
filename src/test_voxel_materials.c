#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "voxel.h"

static int failures = 0;
static void report(const char *name, bool ok)
{
  printf("  %s %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok)
    failures++;
}

int main(void)
{
  printf("=== Voxel Material Completeness ===\n");
  report("COUNT fits in uint8 before WORLD", (int)VOXEL_COUNT < (int)VOXEL_WORLD);
  report("glass helper includes stained",
         voxel_type_is_glass(VOXEL_GLASS) && voxel_type_is_glass(VOXEL_GLASS_BLUE) &&
             !voxel_type_is_glass(VOXEL_STONE));
  report("wool helper includes dyes",
         voxel_type_is_wool(VOXEL_WOOL) && voxel_type_is_wool(VOXEL_WOOL_RED) &&
             !voxel_type_is_wool(VOXEL_CLOTH));
  report("construction helper covers plank/cobble",
         voxel_type_is_construction(VOXEL_PLANK) && voxel_type_is_construction(VOXEL_COBBLE) &&
             voxel_type_is_construction(VOXEL_THATCH));
  report("door and roof tile are construction",
         voxel_type_is_construction(VOXEL_DOOR) && voxel_type_is_construction(VOXEL_ROOF_TILE));
  report("stained glass is translucent", voxel_type_opacity(VOXEL_GLASS_RED) < 1.0f);
  report("plank is opaque", voxel_type_opacity(VOXEL_PLANK) >= 1.0f);
  report("names resolve for new materials",
         strcmp(voxel_type_name(VOXEL_PLANK), "PLANK") == 0 &&
             strcmp(voxel_type_name(VOXEL_WOOL_YELLOW), "WOOL_YELLOW") == 0 &&
             strcmp(voxel_type_name(VOXEL_LEATHER), "LEATHER") == 0 &&
             strcmp(voxel_type_name(VOXEL_FEATHER), "FEATHER") == 0 &&
             strcmp(voxel_type_name(VOXEL_PAPER), "PAPER") == 0 &&
             strcmp(voxel_type_name(VOXEL_RUBBER), "RUBBER") == 0 &&
             strcmp(voxel_type_name(VOXEL_DOOR), "DOOR") == 0 &&
             strcmp(voxel_type_name(VOXEL_ROOF_TILE), "ROOF_TILE") == 0);
  report("cloth/plastic no longer UNKNOWN",
         strcmp(voxel_type_name(VOXEL_CLOTH), "CLOTH") == 0 &&
             strcmp(voxel_type_name(VOXEL_PLASTIC), "PLASTIC") == 0);
  report("ash has mass", voxel_type_mass_kg(VOXEL_ASH) > 0.0f);
  report("wool mass is lighter than stone",
         voxel_type_mass_kg(VOXEL_WOOL_WHITE) < voxel_type_mass_kg(VOXEL_STONE));

  printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures,
         failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
