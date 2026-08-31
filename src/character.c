#include "character.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

// For SHA256 implementation
#include "noise-c/src/crypto/sha2/sha256.h"

// Calculate SHA256 hash of a string
static void calculate_sha256(const char* input, uint8_t output[32]) {
    sha256_context_t ctx;
    sha256_reset(&ctx);
    sha256_update(&ctx, (const uint8_t*)input, strlen(input));
    sha256_finish(&ctx, output);
}

// Magic bytes for player ID (0xc0d3f33d in little endian)
#define FABRIC_MAGIC 0xc0d3f33d
static const uint8_t PLAYER_ID_MAGIC[4] = {0x3d, 0xf3, 0xd3, 0xc0};

// Double SHA256 hash function (BIP 143 compliant)
void double_sha256_hash(const uint8_t* input, size_t input_len, uint8_t* output) {
    (void)input_len; // Suppress unused parameter warning
    uint8_t intermediate[32];

    // First SHA256
    calculate_sha256((const char*)input, intermediate);

    // Second SHA256 (double hash)
    calculate_sha256((const char*)intermediate, output);
}

// Create player ID preimage
PlayerIdPreimage* player_id_preimage_create(uint32_t version, const char* parent_hash,
                                          const char* author_hash, uint32_t type,
                                          uint32_t size, const char* content_hash,
                                          const char* signature) {
    PlayerIdPreimage* preimage = malloc(sizeof(PlayerIdPreimage));
    if (!preimage) return NULL;

    // Set magic bytes
    memcpy(preimage->magic, PLAYER_ID_MAGIC, 4);

    // Set version (little endian)
    preimage->version[0] = version & 0xFF;
    preimage->version[1] = (version >> 8) & 0xFF;
    preimage->version[2] = (version >> 16) & 0xFF;
    preimage->version[3] = (version >> 24) & 0xFF;

    // Set parent hash (32 bytes)
    if (parent_hash && strlen(parent_hash) == 64) {
        for (int i = 0; i < 32; i++) {
            char hex[3] = {parent_hash[i*2], parent_hash[i*2+1], 0};
            preimage->parent[i] = (uint8_t)strtol(hex, NULL, 16);
        }
    } else {
        memset(preimage->parent, 0, 32);
    }

    // Set author hash (32 bytes)
    if (author_hash && strlen(author_hash) == 64) {
        for (int i = 0; i < 32; i++) {
            char hex[3] = {author_hash[i*2], author_hash[i*2+1], 0};
            preimage->author[i] = (uint8_t)strtol(hex, NULL, 16);
        }
    } else {
        memset(preimage->author, 0, 32);
    }

    // Set type (little endian)
    preimage->type[0] = type & 0xFF;
    preimage->type[1] = (type >> 8) & 0xFF;
    preimage->type[2] = (type >> 16) & 0xFF;
    preimage->type[3] = (type >> 24) & 0xFF;

    // Set size (little endian)
    preimage->size[0] = size & 0xFF;
    preimage->size[1] = (size >> 8) & 0xFF;
    preimage->size[2] = (size >> 16) & 0xFF;
    preimage->size[3] = (size >> 24) & 0xFF;

    // Set content hash (32 bytes)
    if (content_hash && strlen(content_hash) == 64) {
        for (int i = 0; i < 32; i++) {
            char hex[3] = {content_hash[i*2], content_hash[i*2+1], 0};
            uint8_t byte = (uint8_t)strtol(hex, NULL, 16);
            preimage->hash[i] = byte;
        }
    } else {
        memset(preimage->hash, 0, 32);
    }

    // Set signature (64 bytes)
    if (signature && strlen(signature) == 128) {
        for (int i = 0; i < 64; i++) {
            char hex[3] = {signature[i*2], signature[i*2+1], 0};
            preimage->signature[i] = (uint8_t)strtol(hex, NULL, 16);
        }
    } else {
        memset(preimage->signature, 0, 64);
    }

    return preimage;
}

// Destroy player ID preimage
void player_id_preimage_destroy(PlayerIdPreimage* preimage) {
    if (preimage) {
        free(preimage);
    }
}

// Serialize preimage to byte array
uint8_t* player_id_preimage_serialize(const PlayerIdPreimage* preimage, size_t* size) {
    if (!preimage || !size) return NULL;

    *size = sizeof(PlayerIdPreimage);
    uint8_t* data = malloc(*size);
    if (!data) return NULL;

    memcpy(data, preimage, *size);
    return data;
}

// Deserialize preimage from byte array
PlayerIdPreimage* player_id_preimage_deserialize(const uint8_t* data, size_t size) {
    if (!data || size != sizeof(PlayerIdPreimage)) return NULL;

    PlayerIdPreimage* preimage = malloc(sizeof(PlayerIdPreimage));
    if (!preimage) return NULL;

    memcpy(preimage, data, size);
    return preimage;
}

// Create player ID from character name and world seed
PlayerId* player_id_create(const char* character_name, const char* world_seed, uint32_t version) {
    if (!character_name || !world_seed) return NULL;

    PlayerId* player_id = malloc(sizeof(PlayerId));
    if (!player_id) return NULL;

    // Generate content hash from character name and world seed
    char content_input[256];
    snprintf(content_input, sizeof(content_input), "%s:%s", character_name, world_seed);

    uint8_t content_hash[32];
    calculate_sha256(content_input, content_hash);

    // Convert content hash to hex string
    char content_hash_hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(content_hash_hex + (i * 2), "%02x", content_hash[i]);
    }
    content_hash_hex[64] = '\0';

    // Create preimage with default values
    player_id->preimage = *player_id_preimage_create(
        version,                    // version
        "0000000000000000000000000000000000000000000000000000000000000000", // parent (zero hash)
        "0000000000000000000000000000000000000000000000000000000000000000", // author (zero hash)
        1,                          // type (player character)
        strlen(character_name),     // size (character name length)
        content_hash_hex,           // content hash
        "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000" // signature (zero)
    );

    // Generate double SHA256 hash of preimage
    uint8_t serialized_preimage[sizeof(PlayerIdPreimage)];

    // Manual serialization to ensure proper layout
    int offset = 0;

    // Magic (4 bytes)
    memcpy(serialized_preimage + offset, player_id->preimage.magic, 4);
    offset += 4;

    // Version (4 bytes)
    memcpy(serialized_preimage + offset, player_id->preimage.version, 4);
    offset += 4;

    // Parent hash (32 bytes)
    memcpy(serialized_preimage + offset, player_id->preimage.parent, 32);
    offset += 32;

    // Author hash (32 bytes)
    memcpy(serialized_preimage + offset, player_id->preimage.author, 32);
    offset += 32;

    // Type (4 bytes)
    memcpy(serialized_preimage + offset, player_id->preimage.type, 4);
    offset += 4;

    // Size (4 bytes)
    memcpy(serialized_preimage + offset, player_id->preimage.size, 4);
    offset += 4;

    // Content hash (32 bytes) - this should be at offset 80
    memcpy(serialized_preimage + offset, player_id->preimage.hash, 32);
    offset += 32;

    // Signature (64 bytes)
    memcpy(serialized_preimage + offset, player_id->preimage.signature, 64);
    offset += 64;

    double_sha256_hash(serialized_preimage, sizeof(PlayerIdPreimage), player_id->id);

    return player_id;
}

// Destroy player ID
void player_id_destroy(PlayerId* player_id) {
    if (player_id) {
        free(player_id);
    }
}

// Validate player ID
bool player_id_validate(const PlayerId* player_id) {
    if (!player_id) return false;

    // Check magic bytes
    if (memcmp(player_id->preimage.magic, PLAYER_ID_MAGIC, 4) != 0) {
        return false;
    }

    // Verify the ID hash matches the preimage
    uint8_t expected_id[32];
    uint8_t serialized_preimage[sizeof(PlayerIdPreimage)];
    memcpy(serialized_preimage, &player_id->preimage, sizeof(PlayerIdPreimage));

    double_sha256_hash(serialized_preimage, sizeof(PlayerIdPreimage), expected_id);

    return memcmp(player_id->id, expected_id, 32) == 0;
}

// Convert player ID to hex string
char* player_id_to_hex(const PlayerId* player_id) {
    if (!player_id) return NULL;

    char* hex_string = malloc(65); // 32 bytes * 2 + null terminator
    if (!hex_string) return NULL;

    for (int i = 0; i < 32; i++) {
        sprintf(hex_string + (i * 2), "%02x", player_id->id[i]);
    }
    hex_string[64] = '\0';

    return hex_string;
}

// Create player ID from hex string
PlayerId* player_id_from_hex(const char* hex_string) {
    if (!hex_string || strlen(hex_string) != 64) return NULL;

    PlayerId* player_id = malloc(sizeof(PlayerId));
    if (!player_id) return NULL;

    // Parse hex string to ID bytes
    for (int i = 0; i < 32; i++) {
        char hex[3] = {hex_string[i*2], hex_string[i*2+1], 0};
        player_id->id[i] = (uint8_t)strtol(hex, NULL, 16);
    }

    // Note: This creates an incomplete player ID since we don't have the preimage
    // In a real implementation, you'd need to store/retrieve the preimage separately
    memset(&player_id->preimage, 0, sizeof(PlayerIdPreimage));

    return player_id;
}

// Compare two player IDs
bool player_id_equals(const PlayerId* id1, const PlayerId* id2) {
    if (!id1 || !id2) return false;
    return memcmp(id1->id, id2->id, 32) == 0;
}

// Create characters directory if it doesn't exist
bool character_ensure_directory()
{
  struct stat st = {0};
  if (stat("characters", &st) == -1)
  {
    if (mkdir("characters", 0700) == 0)
    {
      printf("Created characters directory\n");
      return true;
    }
    else
    {
      printf("Failed to create characters directory\n");
      return false;
    }
  }
  return true;
}

// Get character save filename
void character_get_save_filename(const char *character_name, char *filename, int max_len)
{
  (void)character_name; // Suppress unused parameter warning
  snprintf(filename, max_len, "characters/latest.save");
}

void character_get_timestamped_save_filename(const char *character_name, char *filename, int max_len)
{
  // Get current timestamp in ISO format
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%S", tm_info);

  snprintf(filename, max_len, "characters/%s.%s.save", character_name, timestamp);
}

void character_log_save_files()
{
  printf("=== Available Character Save Files ===\n");

  if (!character_ensure_directory()) {
    printf("Failed to access characters directory\n");
    return;
  }

  DIR *dir = opendir("characters");
  if (!dir) {
    printf("No characters directory found\n");
    return;
  }

  struct dirent *entry;
  int file_count = 0;

  while ((entry = readdir(dir)) != NULL) {
    // Check if it's a .save file
    char *ext = strrchr(entry->d_name, '.');
    if (ext && strcmp(ext, ".save") == 0) {
      file_count++;

      // Get file info
      char filepath[512];
      snprintf(filepath, sizeof(filepath), "characters/%s", entry->d_name);

      struct stat st;
      if (stat(filepath, &st) == 0) {
        struct tm *tm_info = localtime(&st.st_mtime);
        char mod_time[32];
        strftime(mod_time, sizeof(mod_time), "%Y-%m-%d %H:%M:%S", tm_info);

        printf("  %s (modified: %s, size: %ld bytes)\n",
               entry->d_name, mod_time, (long)st.st_size);
      } else {
        printf("  %s (file info unavailable)\n", entry->d_name);
      }
    }
  }

  closedir(dir);

  if (file_count == 0) {
    printf("  No save files found\n");
  } else {
    printf("Total save files: %d\n", file_count);
  }
  printf("=====================================\n");
}

// Save character game data
bool character_save_game(const char *character_name, int x, int y, int z,
                         int strength, int dexterity, int intelligence,
                         int wisdom, int constitution, int luck,
                         int experience_points, int gold, const char *world_seed,
                         int attribute_points, int skill_points, uint32_t unlocked_skills,
                         const uint8_t *skill_ranks)
{

  if (!character_ensure_directory())
  {
    return false;
  }

  // Get current timestamp
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

  // Generate player ID
  PlayerId* player_id = player_id_create(character_name, world_seed, 1);
  if (!player_id) {
    printf("Failed to generate player ID\n");
    return false;
  }

  // Write save data
  CharacterSave save;
  memset(&save, 0, sizeof(CharacterSave));

  strncpy(save.name, character_name, sizeof(save.name) - 1);
  save.x = x;
  save.y = y;
  save.z = z;
  save.strength = strength;
  save.dexterity = dexterity;
  save.intelligence = intelligence;
  save.wisdom = wisdom;
  save.constitution = constitution;
  save.luck = luck;
  save.experience_points = experience_points;
  save.gold = gold;
  save.attribute_points = attribute_points;
  save.skill_points = skill_points;
  save.unlocked_skills = unlocked_skills;
  if (skill_ranks)
    memcpy(save.skill_ranks, skill_ranks, sizeof(save.skill_ranks));
  strncpy(save.world_seed, world_seed, sizeof(save.world_seed) - 1);
  strncpy(save.save_timestamp, timestamp, sizeof(save.save_timestamp) - 1);
  save.is_valid = true;

  // State transition system not yet integrated
  save.state_transitions = NULL;

  // Copy player ID
  // TODO: store this as just `id` for player and actor; player should inherit from actor
  save.player_id = *player_id;

  // Save to latest.save
  char latest_filename[256];
  character_get_save_filename(character_name, latest_filename, sizeof(latest_filename));

  FILE *latest_file = fopen(latest_filename, "wb");
  if (!latest_file) {
    printf("Failed to open latest save file for writing: %s\n", latest_filename);
    player_id_destroy(player_id);
    return false;
  }

  size_t written = fwrite(&save, sizeof(CharacterSave), 1, latest_file);
  fclose(latest_file);

  if (written != 1) {
    printf("Failed to write latest save data\n");
    player_id_destroy(player_id);
    return false;
  }

  // Save to timestamped backup
  char timestamped_filename[256];
  character_get_timestamped_save_filename(character_name, timestamped_filename, sizeof(timestamped_filename));

  FILE *backup_file = fopen(timestamped_filename, "wb");
  if (!backup_file) {
    printf("Failed to open timestamped backup file for writing: %s\n", timestamped_filename);
    player_id_destroy(player_id);
    return false;
  }

  written = fwrite(&save, sizeof(CharacterSave), 1, backup_file);
  fclose(backup_file);

  // Clean up player ID
  player_id_destroy(player_id);

  if (written == 1) {
    printf("Game saved successfully:\n");
    printf("  Latest: %s\n", latest_filename);
    printf("  Backup: %s\n", timestamped_filename);

    // Log available save files
    character_log_save_files();

    return true;
  } else {
    printf("Failed to write timestamped backup data\n");
    return false;
  }
}

// Load character game data
static bool character_read_save_file(const char *path, CharacterSave *save)
{
  if (!path || !save)
    return false;

  FILE *file = fopen(path, "rb");
  if (!file)
    return false;

  memset(save, 0, sizeof(*save));
  // Accept older blobs that omit trailing fields (attribute_points / skill tree).
  size_t n = fread(save, 1, sizeof(*save), file);
  fclose(file);
  if (n < offsetof(CharacterSave, player_id) + sizeof(PlayerId))
    return false;
  return character_validate_save(save);
}

bool character_load_from_path(const char *path, CharacterSave *save)
{
  if (!character_read_save_file(path, save))
  {
    printf("Failed to load save data or invalid save: %s\n", path ? path : "(null)");
    return false;
  }
  printf("Game loaded successfully: %s\n", path);
  return true;
}

bool character_load_game(const char *character_name, CharacterSave *save)
{
  char filename[256];
  character_get_save_filename(character_name, filename, sizeof(filename));
  return character_load_from_path(filename, save);
}

bool character_delete_save_file(const char *path)
{
  if (!path || !path[0])
    return false;
  if (remove(path) == 0)
  {
    printf("Save deleted successfully: %s\n", path);
    return true;
  }
  printf("Failed to delete save: %s\n", path);
  return false;
}

// Delete character save
bool character_delete_save(const char *character_name)
{
  char filename[256];
  character_get_save_filename(character_name, filename, sizeof(filename));
  return character_delete_save_file(filename);
}

static bool character_filename_is_save(const char *name)
{
  if (!name)
    return false;
  size_t n = strlen(name);
  return n > 5 && strcmp(name + n - 5, ".save") == 0;
}

static int character_save_entry_newer(const void *a, const void *b)
{
  const CharacterSaveEntry *ea = (const CharacterSaveEntry *)a;
  const CharacterSaveEntry *eb = (const CharacterSaveEntry *)b;
  if (ea->mtime > eb->mtime)
    return -1;
  if (ea->mtime < eb->mtime)
    return 1;
  return strcmp(eb->filename, ea->filename);
}

int character_list_save_files(CharacterSaveEntry *out, int max)
{
  if (!out || max <= 0)
    return 0;
  if (!character_ensure_directory())
    return 0;

  DIR *dir = opendir("characters");
  if (!dir)
    return 0;

  CharacterSaveEntry scratch[CHARACTER_SAVE_MAX_LIST];
  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && count < CHARACTER_SAVE_MAX_LIST)
  {
    if (!character_filename_is_save(entry->d_name))
      continue;

    CharacterSaveEntry *slot = &scratch[count];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->path, sizeof(slot->path), "characters/%s", entry->d_name);
    strncpy(slot->filename, entry->d_name, sizeof(slot->filename) - 1);

    struct stat st;
    if (stat(slot->path, &st) == 0)
      slot->mtime = st.st_mtime;

    if (!character_read_save_file(slot->path, &slot->data))
      continue;
    count++;
  }
  closedir(dir);

  if (count > 1)
    qsort(scratch, (size_t)count, sizeof(scratch[0]), character_save_entry_newer);
  if (count > max)
    count = max;
  memcpy(out, scratch, (size_t)count * sizeof(scratch[0]));
  return count;
}

// List all saved characters
int character_list_saves(CharacterSave *saves, int max_saves)
{
  CharacterSaveEntry files[CHARACTER_SAVE_MAX_LIST];
  int n = character_list_save_files(files, CHARACTER_SAVE_MAX_LIST);
  if (n > max_saves)
    n = max_saves;
  for (int i = 0; i < n; i++)
    saves[i] = files[i].data;
  return n;
}

// Check if a character save exists
bool character_save_exists(const char *character_name)
{
  char filename[256];
  character_get_save_filename(character_name, filename, sizeof(filename));

  struct stat st = {0};
  return stat(filename, &st) == 0;
}

// Check if any save files exist
bool character_any_saves_exist()
{
  if (!character_ensure_directory())
  {
    return false;
  }

  DIR *dir = opendir("characters");
  if (!dir)
  {
    return false;
  }

  bool found_save = false;
  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL)
  {
    if (strstr(entry->d_name, ".save") != NULL)
    {
      found_save = true;
      break;
    }
  }

  closedir(dir);
  return found_save;
}

// Save to default save file
bool character_save_default(const char *character_name, int x, int y, int z,
                           int strength, int dexterity, int intelligence,
                           int wisdom, int constitution, int luck,
                           int experience_points, int gold, const char *world_seed,
                           int attribute_points, int skill_points, uint32_t unlocked_skills,
                           const uint8_t *skill_ranks)
{
  if (!character_ensure_directory())
  {
    return false;
  }

  // Get current timestamp
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

  // Generate player ID
  PlayerId* player_id = player_id_create(character_name, world_seed, 1);
  if (!player_id) {
    printf("Failed to generate player ID for default save\n");
    return false;
  }

  // Write save data
  CharacterSave save;
  memset(&save, 0, sizeof(CharacterSave));

  strncpy(save.name, character_name, sizeof(save.name) - 1);
  save.x = x;
  save.y = y;
  save.z = z;
  save.strength = strength;
  save.dexterity = dexterity;
  save.intelligence = intelligence;
  save.wisdom = wisdom;
  save.constitution = constitution;
  save.luck = luck;
  save.experience_points = experience_points;
  save.gold = gold;
  save.attribute_points = attribute_points;
  save.skill_points = skill_points;
  save.unlocked_skills = unlocked_skills;
  if (skill_ranks)
    memcpy(save.skill_ranks, skill_ranks, sizeof(save.skill_ranks));
  strncpy(save.world_seed, world_seed, sizeof(save.world_seed) - 1);
  strncpy(save.save_timestamp, timestamp, sizeof(save.save_timestamp) - 1);
  save.is_valid = true;

  // State transition system not yet integrated
  save.state_transitions = NULL;

  // Copy player ID
  save.player_id = *player_id;

  // Save to latest.save
  char latest_filename[256];
  character_get_save_filename(character_name, latest_filename, sizeof(latest_filename));

  FILE *latest_file = fopen(latest_filename, "wb");
  if (!latest_file) {
    printf("Failed to open latest save file for writing: %s\n", latest_filename);
    player_id_destroy(player_id);
    return false;
  }

  size_t written = fwrite(&save, sizeof(CharacterSave), 1, latest_file);
  fclose(latest_file);

  if (written != 1) {
    printf("Failed to write latest save data\n");
    player_id_destroy(player_id);
    return false;
  }

  // Save to timestamped backup
  char timestamped_filename[256];
  character_get_timestamped_save_filename(character_name, timestamped_filename, sizeof(timestamped_filename));

  FILE *backup_file = fopen(timestamped_filename, "wb");
  if (!backup_file) {
    printf("Failed to open timestamped backup file for writing: %s\n", timestamped_filename);
    player_id_destroy(player_id);
    return false;
  }

  written = fwrite(&save, sizeof(CharacterSave), 1, backup_file);
  fclose(backup_file);

  // Clean up player ID
  player_id_destroy(player_id);

  if (written == 1) {
    printf("Default game saved successfully:\n");
    printf("  Latest: %s\n", latest_filename);
    printf("  Backup: %s\n", timestamped_filename);

    // Log available save files
    character_log_save_files();

    return true;
  } else {
    printf("Failed to write timestamped backup data\n");
    return false;
  }
}

// Load from default save file
bool character_load_default(CharacterSave *save)
{
  if (!save)
  {
    return false;
  }

  char filename[256];
  character_get_save_filename("", filename, sizeof(filename));

  FILE *file = fopen(filename, "rb");
  if (!file)
  {
    printf("Failed to open latest save file for reading: %s\n", filename);
    return false;
  }

  memset(save, 0, sizeof(*save));
  size_t read = fread(save, 1, sizeof(CharacterSave), file);
  fclose(file);

  if (read >= offsetof(CharacterSave, player_id) + sizeof(PlayerId) &&
      character_validate_save(save))
  {
    printf("Default game loaded successfully: %s\n", filename);
    return true;
  }
  else
  {
    printf("Failed to load default save data or invalid save\n");
    return false;
  }
}

// Check if default save exists
bool character_default_save_exists()
{
  char filename[256];
  character_get_save_filename("", filename, sizeof(filename));

  struct stat st = {0};
  if (stat(filename, &st) == 0)
  {
    // Check if file is readable and has valid size
    if (st.st_size >= sizeof(CharacterSave))
    {
      return true;
    }
  }
  return false;
}

// Validate character save data
bool character_validate_save(const CharacterSave *save)
{
  if (!save || !save->is_valid)
  {
    return false;
  }

  // Check for reasonable bounds
  if (save->strength < 1 || save->strength > 100 ||
      save->dexterity < 1 || save->dexterity > 100 ||
      save->intelligence < 1 || save->intelligence > 100 ||
      save->wisdom < 1 || save->wisdom > 100 ||
      save->constitution < 1 || save->constitution > 100 ||
      save->luck < 1 || save->luck > 100)
  {
    return false;
  }

  if (save->experience_points < 0 || save->gold < 0 || save->attribute_points < 0 ||
      save->skill_points < 0)
  {
    return false;
  }

  if (strlen(save->name) == 0 || strlen(save->world_seed) == 0)
  {
    return false;
  }

  return true;
}

// ============================================================================
// Player functions (new Actor-based system)
// ============================================================================

// Create a new player that inherits from Actor
Player* player_create(const char* name, const char* description, const char* world_id, const char* world_seed) {
    if (!name || !world_id || !world_seed) return NULL;

    Player* player = malloc(sizeof(Player));
    if (!player) return NULL;

    // Initialize with zeros
    memset(player, 0, sizeof(Player));

    // Create the base Actor
    Actor* base_actor = actor_create(name, description, world_id);
    if (!base_actor) {
        free(player);
        return NULL;
    }

    // Copy the base Actor data
    memcpy(&player->base, base_actor, sizeof(Actor));
    actor_destroy(base_actor);

    // Set player-specific properties
    player->gold = 0;
    strncpy(player->world_seed, world_seed, sizeof(player->world_seed) - 1);
    player->world_seed[sizeof(player->world_seed) - 1] = '\0';

    // Set timestamp
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(player->save_timestamp, sizeof(player->save_timestamp), "%Y-%m-%dT%H:%M:%S", tm_info);

    // Initialize state transitions
    player->state_transitions = NULL; // Will be initialized when needed

    // Create player ID using the existing system
    PlayerId* player_id = player_id_create(name, world_seed, 1);
    if (player_id) {
        memcpy(&player->player_id, player_id, sizeof(PlayerId));
        player_id_destroy(player_id);
    } else {
        // Fallback: create empty player ID
        memset(&player->player_id, 0, sizeof(PlayerId));
    }

    return player;
}

// Destroy a player
void player_destroy(Player* player) {
    if (player) {
        // Clean up state transitions
        if (player->state_transitions) {
            // Note: StateTransitionSystem cleanup would go here
            // For now, just free the pointer
            free(player->state_transitions);
        }
        free(player);
    }
}

// Save player to file
bool player_save(Player* player, const char* filename) {
    if (!player || !filename) return false;

    // Ensure directory exists
    if (!character_ensure_directory()) {
        return false;
    }

    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("Failed to open file for writing: %s\n", filename);
        return false;
    }

    size_t written = fwrite(player, sizeof(Player), 1, file);
    fclose(file);

    if (written == 1) {
        printf("Player saved successfully: %s\n", filename);
        return true;
    } else {
        printf("Failed to write player data\n");
        return false;
    }
}

// Load player from file
Player* player_load(const char* filename) {
    if (!filename) return NULL;

    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open file for reading: %s\n", filename);
        return NULL;
    }

    Player* player = malloc(sizeof(Player));
    if (!player) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(player, sizeof(Player), 1, file);
    fclose(file);

    if (read == 1 && player_validate(player)) {
        printf("Player loaded successfully: %s\n", filename);
        return player;
    } else {
        printf("Failed to load player data or invalid player\n");
        free(player);
        return NULL;
    }
}

// Validate player data
bool player_validate(const Player* player) {
    if (!player) return false;

    // Validate base Actor
    if (!actor_is_valid(&player->base)) {
        return false;
    }

    // Check for reasonable bounds
    if (player->base.strength < 1 || player->base.strength > 100 ||
        player->base.dexterity < 1 || player->base.dexterity > 100 ||
        player->base.intelligence < 1 || player->base.intelligence > 100 ||
        player->base.wisdom < 1 || player->base.wisdom > 100 ||
        player->base.constitution < 1 || player->base.constitution > 100 ||
        player->base.luck < 1 || player->base.luck > 100) {
        return false;
    }

    if (player->base.experience < 0 || player->gold < 0) {
        return false;
    }

    if (strlen(player->base.name) == 0 || strlen(player->world_seed) == 0) {
        return false;
    }

    return true;
}
