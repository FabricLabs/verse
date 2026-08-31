#include "voxel.h"
#include <stdio.h>

int main() {
    // Test that we can access all voxel properties from voxel.h
    printf("Testing voxel canonicalization:\n");

    // Test mass access
    float air_mass = voxel_type_mass_kg(VOXEL_AIR);
    float water_mass = voxel_type_mass_kg(VOXEL_WATER);
    printf("Air mass: %f kg\n", air_mass);
    printf("Water mass: %f kg\n", water_mass);

    // Test opacity access
    float air_opacity = voxel_type_opacity(VOXEL_AIR);
    float stone_opacity = voxel_type_opacity(VOXEL_STONE);
    printf("Air opacity: %f\n", air_opacity);
    printf("Stone opacity: %f\n", stone_opacity);

    // Test rarity access
    int air_rarity = voxel_type_rarity(VOXEL_AIR);
    int gold_rarity = voxel_type_rarity(VOXEL_GOLD);
    printf("Air rarity: %d\n", air_rarity);
    printf("Gold rarity: %d\n", gold_rarity);

    // Test name access
    const char* air_name = voxel_type_name(VOXEL_AIR);
    const char* gold_name = voxel_type_name(VOXEL_GOLD);
    printf("Air name: %s\n", air_name);
    printf("Gold name: %s\n", gold_name);

    // Test constants
    printf("Voxel volume: %f m³\n", VOXEL_VOLUME_M3);
    printf("Voxel side length: %f m\n", VOXEL_METERS_PER_SIDE);

    printf("All voxel properties successfully accessed from voxel.h!\n");
    return 0;
}
