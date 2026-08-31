#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "world.h"

// Generate Fibonacci numbers up to 200
void generate_fibonacci_numbers(uint64_t* fib, int* count) {
    fib[0] = 1;
    fib[1] = 1;
    *count = 2;

    for (int i = 2; i < 100; i++) {
        uint64_t next = fib[i-1] + fib[i-2];
        if (next > 200 || next < fib[i-1]) { // Check for overflow
            break;
        }
        fib[i] = next;
        (*count)++;
    }
}

void test_comprehensive_level_analysis() {
    printf("=== Comprehensive World Level Analysis ===\n\n");

    // Test scenarios
    struct TestScenario {
        const char* name;
        uint32_t history_events;
        uint32_t unique_players;
        uint32_t base_level;
        uint64_t vector_clock;
    };

    struct TestScenario scenarios[] = {
        // Single player scenarios
        {"Brand New World", 0, 1, 0, 1},
        {"Low Activity", 5, 1, 0, 1000},
        {"Medium Activity", 50, 1, 0, 100000},
        {"High Activity", 500, 1, 0, 10000000},
        {"Very High Activity", 5000, 1, 0, 1000000000},

        // Multiple player scenarios
        {"2 Players - Low", 10, 2, 0, 1000},
        {"2 Players - Medium", 100, 2, 0, 100000},
        {"2 Players - High", 1000, 2, 0, 10000000},

        {"3 Players - Low", 15, 3, 0, 1000},
        {"3 Players - Medium", 150, 3, 0, 100000},
        {"3 Players - High", 1500, 3, 0, 10000000},

        {"5 Players - Low", 25, 5, 0, 1000},
        {"5 Players - Medium", 250, 5, 0, 100000},
        {"5 Players - High", 2500, 5, 0, 10000000},

        {"8 Players - Low", 40, 8, 0, 1000},
        {"8 Players - Medium", 400, 8, 0, 100000},
        {"8 Players - High", 4000, 8, 0, 10000000},

        {"13 Players - Low", 65, 13, 0, 1000},
        {"13 Players - Medium", 650, 13, 0, 100000},
        {"13 Players - High", 6500, 13, 0, 10000000},
    };

    printf("=== Standard Activity Scenarios ===\n");
    printf("Scenario\t\t\tScore\t\tLevel\t\tRarity\t\tVector Clock\n");
    printf("--------\t\t\t-----\t\t-----\t\t------\t\t------------\n");

    for (int i = 0; i < sizeof(scenarios)/sizeof(scenarios[0]); i++) {
        World* world = world_create(32, 32, 32);
        if (!world) continue;

        world->history_event_count = scenarios[i].history_events;
        world->unique_player_count = scenarios[i].unique_players;
        world->base_level = scenarios[i].base_level;
        world->vector_clock = scenarios[i].vector_clock;

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

        printf("%-20s\t%.2f\t\t%.2f\t\t%s\t\t%.2e\n",
               scenarios[i].name, score, level, rarity, (double)scenarios[i].vector_clock);

        world_destroy(world);
    }

    // Fibonacci number analysis
    printf("\n=== Fibonacci Number Analysis ===\n");
    uint64_t fib[100];
    int fib_count;
    generate_fibonacci_numbers(fib, &fib_count);

    printf("Fibonacci\t\tScore\t\tLevel\t\tRarity\t\tVector Clock\n");
    printf("----------\t\t-----\t\t-----\t\t------\t\t------------\n");

    for (int i = 0; i < fib_count; i++) {
        World* world = world_create(32, 32, 32);
        if (!world) continue;

        // Use Fibonacci number as vector clock, with proportional events and players
        world->vector_clock = fib[i];
        world->history_event_count = fib[i] / 100; // Proportional events
        world->unique_player_count = (fib[i] % 20) + 1; // 1-20 players
        world->base_level = 0;

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

        printf("Fib(%d)=%-8llu\t%.2f\t\t%.2f\t\t%s\t\t%.2e\n",
               i+1, fib[i], score, level, rarity, (double)world->vector_clock);

        world_destroy(world);
    }

    // Memory overflow analysis
    printf("\n=== Memory Overflow Analysis ===\n");
    printf("Analyzing when vector_clock would overflow uint64_t...\n\n");

    uint64_t max_uint64 = UINT64_MAX;
    printf("Maximum uint64_t value: %llu\n", max_uint64);
    printf("Maximum uint64_t value (scientific): %.2e\n", (double)max_uint64);

    // Calculate what level this would represent
    double max_score = log((double)max_uint64);
    double max_level = log(max_score);

    printf("Maximum possible score: %.2f\n", max_score);
    printf("Maximum possible level: %.2f\n", max_level);

    // Calculate time to reach max uint64_t
    double seconds_to_max = (double)max_uint64;
    double years_to_max = seconds_to_max / (365.25 * 24 * 3600);
    double universe_ages_to_max = years_to_max / 13.8e9;

    printf("Time to reach max uint64_t:\n");
    printf("  Seconds: %.2e\n", seconds_to_max);
    printf("  Years: %.2e\n", years_to_max);
    printf("  Universe ages: %.2e\n", universe_ages_to_max);

    // Test specific high-value scenarios
    printf("\n=== High-Value Scenarios ===\n");
    uint64_t high_values[] = {
        1000000000ULL,      // 1 billion
        10000000000ULL,     // 10 billion
        100000000000ULL,    // 100 billion
        1000000000000ULL,   // 1 trillion
        10000000000000ULL,  // 10 trillion
        100000000000000ULL, // 100 trillion
        1000000000000000ULL, // 1 quadrillion
        10000000000000000ULL, // 10 quadrillion
        100000000000000000ULL, // 100 quadrillion
        1000000000000000000ULL, // 1 quintillion
    };

    printf("High Value\t\tScore\t\tLevel\t\tRarity\t\tYears\n");
    printf("----------\t\t-----\t\t-----\t\t------\t\t-----\n");

    for (int i = 0; i < sizeof(high_values)/sizeof(high_values[0]); i++) {
        World* world = world_create(32, 32, 32);
        if (!world) continue;

        world->vector_clock = high_values[i];
        world->history_event_count = high_values[i] / 1000000; // Proportional events
        world->unique_player_count = (high_values[i] % 100) + 1; // 1-100 players
        world->base_level = 0;

        world_update_score_and_level(world);

        double score = world_get_score(world);
        double level = world_get_level(world);
        double years = (double)high_values[i] / (365.25 * 24 * 3600);

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

        printf("%.2e\t%.2f\t\t%.2f\t\t%s\t\t%.2e\n",
               (double)high_values[i], score, level, rarity, years);

        world_destroy(world);
    }

    printf("\n=== Summary ===\n");
    printf("The level system is designed to scale logarithmically, making it\n");
    printf("practically impossible for any realistic world to reach level 4\n");
    printf("through vector clock alone. The system encourages:\n");
    printf("1. Multiple players visiting\n");
    printf("2. Many history events occurring\n");
    printf("3. Inheritance from active parent worlds\n");
    printf("4. Combination of all factors above\n\n");

    printf("Even with the maximum uint64_t vector clock value, a world would\n");
    printf("only reach level %.2f, which is still in the 'Uncommon' category.\n", max_level);
}

int main() {
    test_comprehensive_level_analysis();
    return 0;
}
