#include <stdio.h>
#include <stdbool.h>
#include "world.h"

int main(int argc, char** argv){
  if (argc < 2){ fprintf(stderr, "usage: %s <path.world>\n", argv[0]); return 2; }
  const char* path = argv[1];
  World* w = world_create(1,1,1);
  if (!w){ fprintf(stderr, "alloc failed\n"); return 3; }
  bool ok = world_load(w, path);
  printf("world_load: %s\n", ok ? "OK" : "FAIL");
  if (ok){ printf("dims: %ux%ux%u\n", (unsigned)w->width, (unsigned)w->height, (unsigned)w->depth); }
  return ok ? 0 : 1;
}


