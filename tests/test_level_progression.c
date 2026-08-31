#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "world.h"

void test_level_progression() {
    printf("=== World Level Progression Analysis ===\n\n");

    // Create a brand-new world
    World* world = world_create(32, 32, 32);
    if (!world) {
        printf("Failed to create world\n");
        return;
    }

    // Set initial conditions: no events, single player
    world->history_event_count = 0;
    world->unique_player_count = 1;
    world->base_level = 0;

    printf("Initial conditions:\n");
    printf("  History Events: %u\n", world->history_event_count);
    printf("  Unique Players: %u\n", world->unique_player_count);
    printf("  Base Level: %u\n", world->base_level);
    printf("\n");

    // Calculate target vector_clock for level 4
    double target_score = exp(4.0); // e^4 ≈ 54.6
    uint64_t target_vector_clock = (uint64_t)exp(target_score);

    printf("Target calculations:\n");
    printf("  Target Level: 4.0\n");
    printf("  Target Score: %.2f (e^4)\n", target_score);
    printf("  Target Vector Clock: %.2e\n", (double)target_vector_clock);
    printf("\n");

    // Show progression at different vector_clock values
    uint64_t test_values[] = {
        1,
        10,
        100,
        1000,
        10000,
        100000,
        1000000,
        10000000,
        100000000,
        1000000000,
        10000000000ULL,
        100000000000ULL,
        1000000000000ULL,
        10000000000000ULL,
        100000000000000ULL,
        1000000000000000ULL,
        10000000000000000ULL,
        100000000000000000ULL,
        1000000000000000000ULL,
        10000000000000000000ULL
    };

    printf("Level progression:\n");
    printf("Vector Clock\t\tScore\t\tLevel\t\tRarity\n");
    printf("-----------\t\t-----\t\t-----\t\t------\n");

    for (int i = 0; i < sizeof(test_values)/sizeof(test_values[0]); i++) {
        world->vector_clock = test_values[i];
        world_update_score_and_level(world);

        double score = world_get_score(world);
        double level = world_get_level(world);

        // Determine rarity category
        const char* rarity = "Epic";
        if (level >= 4.0) {
            rarity = "Common";
        } else if (level >= 3.0) {
            rarity = "Uncommon";
        } else if (level >= 2.0) {
            rarity = "Rare";
        } else if (level >= 1.0) {
            rarity = "Very Rare";
        }

        printf("%.2e\t%.2f\t\t%.2f\t\t%s\n",
               (double)test_values[i], score, level, rarity);

        if (level >= 4.0) {
            printf("\n🎉 REACHED LEVEL 4! Vector clock: %.2e\n", (double)test_values[i]);
            break;
        }
    }

    // Calculate time estimates
    printf("\n=== Time Estimates ===\n");
    printf("Assuming vector_clock increments per second:\n");
    printf("  Target Vector Clock: %.2e\n", (double)target_vector_clock);
    printf("  Time to reach level 4: %.2e seconds\n", (double)target_vector_clock);
    printf("  Time in years: %.2e years\n", (double)target_vector_clock / (365.25 * 24 * 3600));
    printf("  Time in universe ages: %.2e universe ages\n",
           (double)target_vector_clock / (365.25 * 24 * 3600 * 13.8e9));

    printf("\n=== Conclusion ===\n");
    printf("A brand-new world with no events and a single player would need\n");
    printf("a vector_clock of approximately %.2e to reach level 4.\n", (double)target_vector_clock);
    printf("This is effectively impossible in any reasonable timeframe.\n");
    printf("The level system is designed so that brand-new worlds stay at\n");
    printf("level 0 (Epic - 0%% springs) until they accumulate significant activity.\n");

    world_destroy(world);
}

int main() {
    test_level_progression();
    return 0;
}
