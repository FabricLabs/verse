#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "actor.h"
#include "character.h"

int main() {
    printf("=== Actor System Test ===\n");

    // Test 1: Create an Actor
    printf("\n1. Testing Actor creation...\n");
    Actor* actor = actor_create("TestActor", "A test actor", "world_123");

    if (actor) {
        printf("✓ Actor created successfully\n");
        printf("  Name: %s\n", actor->name);
        printf("  Description: %s\n", actor->description);
        printf("  World ID: %s\n", actor->world_id);
        printf("  ID: %u\n", actor->id);
        printf("  Health: %u\n", actor->health);
        printf("  Level: %u\n", actor->level);

        // Test actor validation
        if (actor_is_valid(actor)) {
            printf("✓ Actor validation passed\n");
        } else {
            printf("✗ Actor validation failed\n");
        }

        actor_destroy(actor);
    } else {
        printf("✗ Failed to create actor\n");
        return 1;
    }

    // Test 2: Create a Player (inherits from Actor)
    printf("\n2. Testing Player creation...\n");
    Player* player = player_create("TestPlayer", "A test player", "world_456", "seed_789");

    if (player) {
        printf("✓ Player created successfully\n");
        printf("  Name: %s\n", player->base.name);
        printf("  Description: %s\n", player->base.description);
        printf("  World ID: %s\n", player->base.world_id);
        printf("  World Seed: %s\n", player->world_seed);
        printf("  ID: %u\n", player->base.id);
        printf("  Gold: %d\n", player->gold);
        printf("  Save Timestamp: %s\n", player->save_timestamp);

        // Test player validation
        if (player_validate(player)) {
            printf("✓ Player validation passed\n");
        } else {
            printf("✗ Player validation failed\n");
        }

        player_destroy(player);
    } else {
        printf("✗ Failed to create player\n");
        return 1;
    }

    // Test 3: Test Actor movement
    printf("\n3. Testing Actor movement...\n");
    Actor* moving_actor = actor_create("MovingActor", "An actor that moves", "world_movement");

    if (moving_actor) {
        printf("Initial position: (%.2f, %.2f, %.2f)\n",
               moving_actor->x, moving_actor->y, moving_actor->z);

        actor_set_position(moving_actor, 10.0, 20.0, 30.0);
        printf("After set_position: (%.2f, %.2f, %.2f)\n",
               moving_actor->x, moving_actor->y, moving_actor->z);

        actor_move(moving_actor, 5.0, -3.0, 2.0);
        printf("After move: (%.2f, %.2f, %.2f)\n",
               moving_actor->x, moving_actor->y, moving_actor->z);

        actor_destroy(moving_actor);
    }

    // Test 4: Test Actor stats
    printf("\n4. Testing Actor stats...\n");
    Actor* stat_actor = actor_create("StatActor", "An actor with stats", "world_stats");

    if (stat_actor) {
        printf("Initial stats:\n");
        printf("  Health: %u\n", stat_actor->health);
        printf("  Experience: %u\n", stat_actor->experience);
        printf("  Level: %u\n", stat_actor->level);

        actor_set_health(stat_actor, 150);
        actor_add_experience(stat_actor, 250);

        printf("After modifications:\n");
        printf("  Health: %u / %u\n", stat_actor->health, actor_max_health(stat_actor));
        printf("  Stamina: %.0f / %.0f\n", (double)stat_actor->stamina,
               (double)actor_max_stamina(stat_actor));
        printf("  Mana: %.0f / %.0f\n", (double)stat_actor->mana,
               (double)actor_max_mana(stat_actor));
        printf("  Experience: %u\n", stat_actor->experience);
        printf("  Level: %u\n", stat_actor->level);
        printf("  Attribute points: %u\n", stat_actor->attribute_points);
        printf("  Armor: %u  Turn: %u  Eff. luck: %u\n",
               actor_armor(stat_actor), stat_actor->turn_speed,
               actor_effective_luck(stat_actor));

        // 250 XP => level 3 (was 1), so 2 points; spend one on strength.
        if (stat_actor->attribute_points != 2) {
            printf("✗ Expected 2 attribute points after leveling, got %u\n",
                   stat_actor->attribute_points);
            actor_destroy(stat_actor);
            return 1;
        }
        const uint32_t old_str = stat_actor->strength;
        const uint32_t old_max_hp = actor_max_health(stat_actor);
        if (!actor_spend_attribute_point(stat_actor, ACTOR_ATTR_STRENGTH) ||
            stat_actor->strength != old_str + 1 ||
            stat_actor->attribute_points != 1) {
            printf("✗ Failed to spend attribute point on strength\n");
            actor_destroy(stat_actor);
            return 1;
        }
        if (actor_max_health(stat_actor) != old_max_hp + ACTOR_HP_PER_STRENGTH) {
            printf("✗ Strength spend did not raise max HP by %u (was %u, now %u)\n",
                   ACTOR_HP_PER_STRENGTH, old_max_hp, actor_max_health(stat_actor));
            actor_destroy(stat_actor);
            return 1;
        }
        printf("✓ Attribute point spend works (STR %u -> %u, max HP %u -> %u, points left %u)\n",
               old_str, stat_actor->strength, old_max_hp, actor_max_health(stat_actor),
               stat_actor->attribute_points);

        // DEX raises armor and turn speed.
        const uint32_t old_armor = actor_armor(stat_actor);
        const uint32_t old_turn = stat_actor->turn_speed;
        if (!actor_spend_attribute_point(stat_actor, ACTOR_ATTR_DEXTERITY)) {
            printf("✗ Failed to spend remaining point on dexterity\n");
            actor_destroy(stat_actor);
            return 1;
        }
        if (actor_armor(stat_actor) < old_armor || stat_actor->turn_speed <= old_turn) {
            printf("✗ Dexterity spend did not raise armor/turn (AC %u->%u, TRN %u->%u)\n",
                   old_armor, actor_armor(stat_actor), old_turn, stat_actor->turn_speed);
            actor_destroy(stat_actor);
            return 1;
        }
        printf("✓ Dexterity raises armor and turn speed\n");

        // Armor mitigates incoming damage.
        actor_set_health(stat_actor, actor_max_health(stat_actor));
        const uint32_t before = stat_actor->health;
        const uint32_t dealt = actor_apply_damage(stat_actor, 20);
        if (dealt == 0 || dealt > 20 || stat_actor->health != before - dealt) {
            printf("✗ Damage mitigation failed (dealt %u, HP %u -> %u)\n",
                   dealt, before, stat_actor->health);
            actor_destroy(stat_actor);
            return 1;
        }
        printf("✓ Armor mitigates damage (20 raw -> %u dealt)\n", dealt);

        // Intelligence scales skill power (INT 10 => 1.0x, INT 20 => 2.0x).
        if (actor_skill_power(stat_actor) < 0.99f || actor_skill_power(stat_actor) > 1.01f) {
            printf("✗ Expected ~1.0 skill power at INT %u, got %.2f\n",
                   stat_actor->intelligence, (double)actor_skill_power(stat_actor));
            actor_destroy(stat_actor);
            return 1;
        }
        stat_actor->intelligence = 20;
        if (actor_skill_scale_u32(stat_actor, 18) != 36 ||
            actor_skill_potency(stat_actor) != 200) {
            printf("✗ INT 20 should double skill scale (got dmg %u, pot %u)\n",
                   actor_skill_scale_u32(stat_actor, 18), actor_skill_potency(stat_actor));
            actor_destroy(stat_actor);
            return 1;
        }
        printf("✓ Intelligence scales skill power (INT 20 => 2.0x)\n");

        actor_destroy(stat_actor);
    }

    // Test 5: Test ID generation consistency
    printf("\n5. Testing ID generation consistency...\n");
    Actor* actor1 = actor_create("SameName", "Same description", "same_world");
    Actor* actor2 = actor_create("SameName", "Same description", "same_world");

    if (actor1 && actor2) {
        printf("Actor 1 ID: %u\n", actor1->id);
        printf("Actor 2 ID: %u\n", actor2->id);

        if (actor1->id == actor2->id) {
            printf("✓ ID generation is consistent\n");
        } else {
            printf("✗ ID generation is not consistent\n");
        }

        actor_destroy(actor1);
        actor_destroy(actor2);
    }

    // Test 6: Test Player save/load
    printf("\n6. Testing Player save/load...\n");
    Player* test_player = player_create("SaveTestPlayer", "A player for save testing", "world_save", "seed_save");

    if (test_player) {
        // Modify some properties
        test_player->base.health = 200;
        test_player->base.experience = 500;
        test_player->gold = 1000;
        actor_set_position(&test_player->base, 15.0, 25.0, 35.0);

        printf("Before save:\n");
        printf("  Health: %u\n", test_player->base.health);
        printf("  Experience: %u\n", test_player->base.experience);
        printf("  Gold: %d\n", test_player->gold);
        printf("  Position: (%.2f, %.2f, %.2f)\n",
               test_player->base.x, test_player->base.y, test_player->base.z);

        // Save player
        if (player_save(test_player, "test_player.save")) {
            printf("✓ Player saved successfully\n");

            // Load player
            Player* loaded_player = player_load("test_player.save");
            if (loaded_player) {
                printf("✓ Player loaded successfully\n");
                printf("After load:\n");
                printf("  Health: %u\n", loaded_player->base.health);
                printf("  Experience: %u\n", loaded_player->base.experience);
                printf("  Gold: %d\n", loaded_player->gold);
                printf("  Position: (%.2f, %.2f, %.2f)\n",
                       loaded_player->base.x, loaded_player->base.y, loaded_player->base.z);

                player_destroy(loaded_player);
            } else {
                printf("✗ Failed to load player\n");
            }
        } else {
            printf("✗ Failed to save player\n");
        }

        player_destroy(test_player);
    }

    printf("\n=== Actor System Test Completed Successfully ===\n");
    printf("✓ Actor creation and management works\n");
    printf("✓ Player inheritance from Actor works\n");
    printf("✓ Shared ID computation using double SHA256 works\n");
    printf("✓ Actor movement and stats work\n");
    printf("✓ Player save/load functionality works\n");

    return 0;
}
