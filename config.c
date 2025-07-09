#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <cjson/cJSON.h>
#include "uphonor.h"

/* Helper function to convert enum to string */
static const char *holo_state_to_string(enum holo_state state)
{
  switch (state)
  {
  case HOLO_STATE_IDLE:
    return "IDLE";
  case HOLO_STATE_PLAYING:
    return "PLAYING";
  case HOLO_STATE_STOPPED:
    return "STOPPED";
  default:
    return "UNKNOWN";
  }
}

static enum holo_state string_to_holo_state(const char *str)
{
  if (strcmp(str, "IDLE") == 0)
    return HOLO_STATE_IDLE;
  if (strcmp(str, "PLAYING") == 0)
    return HOLO_STATE_PLAYING;
  if (strcmp(str, "STOPPED") == 0)
    return HOLO_STATE_STOPPED;
  return HOLO_STATE_IDLE; // Default
}

static const char *loop_state_to_string(enum loop_state state)
{
  switch (state)
  {
  case LOOP_STATE_IDLE:
    return "IDLE";
  case LOOP_STATE_RECORDING:
    return "RECORDING";
  case LOOP_STATE_PLAYING:
    return "PLAYING";
  case LOOP_STATE_STOPPED:
    return "STOPPED";
  default:
    return "UNKNOWN";
  }
}

static enum loop_state string_to_loop_state(const char *str)
{
  if (strcmp(str, "IDLE") == 0)
    return LOOP_STATE_IDLE;
  if (strcmp(str, "RECORDING") == 0)
    return LOOP_STATE_RECORDING;
  if (strcmp(str, "PLAYING") == 0)
    return LOOP_STATE_PLAYING;
  if (strcmp(str, "STOPPED") == 0)
    return LOOP_STATE_STOPPED;
  return LOOP_STATE_IDLE; // Default
}

static const char *playback_mode_to_string(enum playback_mode mode)
{
  switch (mode)
  {
  case PLAYBACK_MODE_NORMAL:
    return "NORMAL";
  case PLAYBACK_MODE_TRIGGER:
    return "TRIGGER";
  default:
    return "UNKNOWN";
  }
}

static enum playback_mode string_to_playback_mode(const char *str)
{
  if (strcmp(str, "NORMAL") == 0)
    return PLAYBACK_MODE_NORMAL;
  if (strcmp(str, "TRIGGER") == 0)
    return PLAYBACK_MODE_TRIGGER;
  return PLAYBACK_MODE_TRIGGER; // Default
}

/* Create JSON object for global state */
static cJSON *create_global_state_json(struct data *data)
{
  cJSON *global = cJSON_CreateObject();
  if (!global)
    return NULL;

  cJSON_AddStringToObject(global, "version", CONFIG_VERSION);
  cJSON_AddNumberToObject(global, "volume", data->volume);
  cJSON_AddNumberToObject(global, "playback_speed", data->playback_speed);
  cJSON_AddNumberToObject(global, "pitch_shift", data->pitch_shift);
  cJSON_AddBoolToObject(global, "rubberband_enabled", data->rubberband_enabled);
  cJSON_AddStringToObject(global, "current_state", holo_state_to_string(data->current_state));
  cJSON_AddStringToObject(global, "playback_mode", playback_mode_to_string(data->current_playback_mode));

  /* Sync mode settings */
  cJSON_AddBoolToObject(global, "sync_mode_enabled", data->sync_mode_enabled);
  cJSON_AddNumberToObject(global, "pulse_loop_note", data->pulse_loop_note == 255 ? -1 : data->pulse_loop_note);
  cJSON_AddNumberToObject(global, "pulse_loop_duration", data->pulse_loop_duration);
  cJSON_AddNumberToObject(global, "sync_cutoff_percentage", data->sync_cutoff_percentage);
  cJSON_AddNumberToObject(global, "sync_recording_cutoff_percentage", data->sync_recording_cutoff_percentage);

  /* Global loop management */
  cJSON_AddNumberToObject(global, "active_loop_count", data->active_loop_count);
  cJSON_AddNumberToObject(global, "currently_recording_note",
                          data->currently_recording_note == 255 ? -1 : data->currently_recording_note);

  return global;
}

/* Create JSON array for memory loops */
static cJSON *create_memory_loops_json(struct data *data, bool active_only)
{
  cJSON *loops_array = cJSON_CreateArray();
  if (!loops_array)
    return NULL;

  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];

    /* Skip loops with no content if active_only is true */
    if (active_only && !loop->loop_ready && loop->recorded_frames == 0 &&
        loop->current_state == LOOP_STATE_IDLE)
    {
      continue;
    }

    cJSON *loop_obj = cJSON_CreateObject();
    if (!loop_obj)
    {
      cJSON_Delete(loops_array);
      return NULL;
    }

    cJSON_AddNumberToObject(loop_obj, "midi_note", loop->midi_note);
    cJSON_AddStringToObject(loop_obj, "state", loop_state_to_string(loop->current_state));
    cJSON_AddNumberToObject(loop_obj, "volume", loop->volume);
    cJSON_AddStringToObject(loop_obj, "filename", loop->loop_filename);
    cJSON_AddNumberToObject(loop_obj, "recorded_frames", loop->recorded_frames);
    cJSON_AddNumberToObject(loop_obj, "playback_position", loop->playback_position);
    cJSON_AddNumberToObject(loop_obj, "buffer_size", loop->buffer_size);
    cJSON_AddNumberToObject(loop_obj, "sample_rate", loop->sample_rate);

    /* Boolean flags */
    cJSON_AddBoolToObject(loop_obj, "loop_ready", loop->loop_ready);
    cJSON_AddBoolToObject(loop_obj, "recording_to_memory", loop->recording_to_memory);
    cJSON_AddBoolToObject(loop_obj, "is_playing", loop->is_playing);
    cJSON_AddBoolToObject(loop_obj, "pending_record", loop->pending_record);
    cJSON_AddBoolToObject(loop_obj, "pending_stop", loop->pending_stop);
    cJSON_AddBoolToObject(loop_obj, "pending_start", loop->pending_start);

    cJSON_AddItemToArray(loops_array, loop_obj);
  }

  return loops_array;
}

/* Parse global state from JSON */
static config_result_t parse_global_state_json(struct data *data, cJSON *global)
{
  if (!cJSON_IsObject(global))
    return CONFIG_ERROR_INVALID_DATA;

  /* Check version compatibility */
  cJSON *version = cJSON_GetObjectItemCaseSensitive(global, "version");
  if (cJSON_IsString(version))
  {
    if (strcmp(version->valuestring, CONFIG_VERSION) != 0)
    {
      printf("Warning: Configuration version mismatch. Expected %s, got %s\n",
             CONFIG_VERSION, version->valuestring);
    }
  }

  /* Parse numeric values */
  cJSON *item;
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "volume")) && cJSON_IsNumber(item))
  {
    data->volume = (float)item->valuedouble;
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "playback_speed")) && cJSON_IsNumber(item))
  {
    data->playback_speed = (float)item->valuedouble;
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "pitch_shift")) && cJSON_IsNumber(item))
  {
    data->pitch_shift = (float)item->valuedouble;
  }

  /* Parse boolean values */
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "rubberband_enabled")) && cJSON_IsBool(item))
  {
    data->rubberband_enabled = cJSON_IsTrue(item);
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "sync_mode_enabled")) && cJSON_IsBool(item))
  {
    data->sync_mode_enabled = cJSON_IsTrue(item);
  }

  /* Parse string enum values */
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "current_state")) && cJSON_IsString(item))
  {
    data->current_state = string_to_holo_state(item->valuestring);
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "playback_mode")) && cJSON_IsString(item))
  {
    data->current_playback_mode = string_to_playback_mode(item->valuestring);
  }

  /* Parse sync mode settings */
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "pulse_loop_note")) && cJSON_IsNumber(item))
  {
    int note = (int)item->valuedouble;
    data->pulse_loop_note = (note < 0 || note > 127) ? 255 : (uint8_t)note;
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "pulse_loop_duration")) && cJSON_IsNumber(item))
  {
    data->pulse_loop_duration = (uint32_t)item->valuedouble;
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "sync_cutoff_percentage")) && cJSON_IsNumber(item))
  {
    data->sync_cutoff_percentage = (float)item->valuedouble;
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "sync_recording_cutoff_percentage")) && cJSON_IsNumber(item))
  {
    data->sync_recording_cutoff_percentage = (float)item->valuedouble;
  }

  /* Parse loop management values */
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "active_loop_count")) && cJSON_IsNumber(item))
  {
    data->active_loop_count = (uint8_t)item->valuedouble;
  }
  if ((item = cJSON_GetObjectItemCaseSensitive(global, "currently_recording_note")) && cJSON_IsNumber(item))
  {
    int note = (int)item->valuedouble;
    data->currently_recording_note = (note < 0 || note > 127) ? 255 : (uint8_t)note;
  }

  return CONFIG_SUCCESS;
}

/* Parse memory loops from JSON */
static config_result_t parse_memory_loops_json(struct data *data, cJSON *loops_array)
{
  if (!cJSON_IsArray(loops_array))
    return CONFIG_ERROR_INVALID_DATA;

  /* First, reset all loops to default state */
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->buffer)
    {
      /* Preserve buffer but reset state */
      loop->recorded_frames = 0;
      loop->playback_position = 0;
      loop->loop_ready = false;
      loop->recording_to_memory = false;
      loop->is_playing = false;
      loop->pending_record = false;
      loop->pending_stop = false;
      loop->pending_start = false;
      loop->current_state = LOOP_STATE_IDLE;
      loop->volume = 1.0f;
      memset(loop->loop_filename, 0, sizeof(loop->loop_filename));
    }
  }

  /* Parse each loop from the JSON array */
  cJSON *loop_json = NULL;
  cJSON_ArrayForEach(loop_json, loops_array)
  {
    if (!cJSON_IsObject(loop_json))
      continue;

    cJSON *midi_note_item = cJSON_GetObjectItemCaseSensitive(loop_json, "midi_note");
    if (!cJSON_IsNumber(midi_note_item))
      continue;

    int midi_note = (int)midi_note_item->valuedouble;
    if (midi_note < 0 || midi_note >= 128)
      continue;

    struct memory_loop *loop = &data->memory_loops[midi_note];
    if (!loop->buffer)
      continue; /* Skip if buffer not allocated */

    /* Parse loop properties */
    cJSON *item;
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "state")) && cJSON_IsString(item))
    {
      loop->current_state = string_to_loop_state(item->valuestring);
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "volume")) && cJSON_IsNumber(item))
    {
      loop->volume = (float)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "filename")) && cJSON_IsString(item))
    {
      strncpy(loop->loop_filename, item->valuestring, sizeof(loop->loop_filename) - 1);
      loop->loop_filename[sizeof(loop->loop_filename) - 1] = '\0';
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "recorded_frames")) && cJSON_IsNumber(item))
    {
      loop->recorded_frames = (uint32_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "playback_position")) && cJSON_IsNumber(item))
    {
      loop->playback_position = (uint32_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "sample_rate")) && cJSON_IsNumber(item))
    {
      loop->sample_rate = (uint32_t)item->valuedouble;
    }

    /* Parse boolean flags */
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "loop_ready")) && cJSON_IsBool(item))
    {
      loop->loop_ready = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "recording_to_memory")) && cJSON_IsBool(item))
    {
      loop->recording_to_memory = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "is_playing")) && cJSON_IsBool(item))
    {
      loop->is_playing = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "pending_record")) && cJSON_IsBool(item))
    {
      loop->pending_record = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "pending_stop")) && cJSON_IsBool(item))
    {
      loop->pending_stop = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItemCaseSensitive(loop_json, "pending_start")) && cJSON_IsBool(item))
    {
      loop->pending_start = cJSON_IsTrue(item);
    }
  }

  return CONFIG_SUCCESS;
}

config_result_t config_save_state(struct data *data, const char *filename)
{
  if (!data)
    return CONFIG_ERROR_INVALID_DATA;

  const char *config_file = filename ? filename : DEFAULT_CONFIG_FILENAME;

  /* Create root JSON object */
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return CONFIG_ERROR_MEMORY;

  /* Add global state */
  cJSON *global_state = create_global_state_json(data);
  if (!global_state)
  {
    cJSON_Delete(root);
    return CONFIG_ERROR_MEMORY;
  }
  cJSON_AddItemToObject(root, "global_state", global_state);

  /* Add memory loops */
  cJSON *memory_loops = create_memory_loops_json(data, false);
  if (!memory_loops)
  {
    cJSON_Delete(root);
    return CONFIG_ERROR_MEMORY;
  }
  cJSON_AddItemToObject(root, "memory_loops", memory_loops);

  /* Add timestamp */
  time_t now = time(NULL);
  char timestamp[64];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
  cJSON_AddStringToObject(root, "saved_at", timestamp);

  /* Convert to string and write to file */
  char *json_string = cJSON_Print(root);
  cJSON_Delete(root);

  if (!json_string)
    return CONFIG_ERROR_MEMORY;

  FILE *file = fopen(config_file, "w");
  if (!file)
  {
    free(json_string);
    return CONFIG_ERROR_WRITE_FAILED;
  }

  size_t json_length = strlen(json_string);
  size_t written = fwrite(json_string, 1, json_length, file);
  fclose(file);
  free(json_string);

  return (written == json_length) ? CONFIG_SUCCESS : CONFIG_ERROR_WRITE_FAILED;
}

config_result_t config_load_state(struct data *data, const char *filename)
{
  if (!data)
    return CONFIG_ERROR_INVALID_DATA;

  const char *config_file = filename ? filename : DEFAULT_CONFIG_FILENAME;

  /* Check if file exists */
  if (access(config_file, R_OK) != 0)
  {
    return CONFIG_ERROR_FILE_NOT_FOUND;
  }

  /* Read file contents */
  FILE *file = fopen(config_file, "r");
  if (!file)
    return CONFIG_ERROR_FILE_NOT_FOUND;

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0)
  {
    fclose(file);
    return CONFIG_ERROR_PARSE_FAILED;
  }

  char *file_contents = malloc(file_size + 1);
  if (!file_contents)
  {
    fclose(file);
    return CONFIG_ERROR_MEMORY;
  }

  size_t read_size = fread(file_contents, 1, file_size, file);
  fclose(file);
  file_contents[read_size] = '\0';

  /* Parse JSON */
  cJSON *root = cJSON_Parse(file_contents);
  free(file_contents);

  if (!root)
    return CONFIG_ERROR_PARSE_FAILED;

  config_result_t result = CONFIG_SUCCESS;

  /* Parse global state */
  cJSON *global_state = cJSON_GetObjectItemCaseSensitive(root, "global_state");
  if (global_state)
  {
    result = parse_global_state_json(data, global_state);
    if (result != CONFIG_SUCCESS)
    {
      cJSON_Delete(root);
      return result;
    }
  }

  /* Parse memory loops */
  cJSON *memory_loops = cJSON_GetObjectItemCaseSensitive(root, "memory_loops");
  if (memory_loops)
  {
    result = parse_memory_loops_json(data, memory_loops);
  }

  cJSON_Delete(root);
  return result;
}

config_result_t config_save_active_loops_only(struct data *data, const char *filename)
{
  if (!data)
    return CONFIG_ERROR_INVALID_DATA;

  const char *config_file = filename ? filename : "uphonor_active_loops.json";

  /* Create root JSON object */
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return CONFIG_ERROR_MEMORY;

  /* Add global state */
  cJSON *global_state = create_global_state_json(data);
  if (!global_state)
  {
    cJSON_Delete(root);
    return CONFIG_ERROR_MEMORY;
  }
  cJSON_AddItemToObject(root, "global_state", global_state);

  /* Add only active memory loops */
  cJSON *memory_loops = create_memory_loops_json(data, true);
  if (!memory_loops)
  {
    cJSON_Delete(root);
    return CONFIG_ERROR_MEMORY;
  }
  cJSON_AddItemToObject(root, "memory_loops", memory_loops);

  /* Add timestamp */
  time_t now = time(NULL);
  char timestamp[64];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
  cJSON_AddStringToObject(root, "saved_at", timestamp);

  /* Convert to string and write to file */
  char *json_string = cJSON_Print(root);
  cJSON_Delete(root);

  if (!json_string)
    return CONFIG_ERROR_MEMORY;

  FILE *file = fopen(config_file, "w");
  if (!file)
  {
    free(json_string);
    return CONFIG_ERROR_WRITE_FAILED;
  }

  size_t json_length = strlen(json_string);
  size_t written = fwrite(json_string, 1, json_length, file);
  fclose(file);
  free(json_string);

  return (written == json_length) ? CONFIG_SUCCESS : CONFIG_ERROR_WRITE_FAILED;
}

config_result_t config_validate_file(const char *filename)
{
  if (!filename)
    return CONFIG_ERROR_INVALID_DATA;

  /* Check if file exists and is readable */
  if (access(filename, R_OK) != 0)
  {
    return CONFIG_ERROR_FILE_NOT_FOUND;
  }

  /* Try to parse the file */
  FILE *file = fopen(filename, "r");
  if (!file)
    return CONFIG_ERROR_FILE_NOT_FOUND;

  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0)
  {
    fclose(file);
    return CONFIG_ERROR_PARSE_FAILED;
  }

  char *file_contents = malloc(file_size + 1);
  if (!file_contents)
  {
    fclose(file);
    return CONFIG_ERROR_MEMORY;
  }

  size_t read_size = fread(file_contents, 1, file_size, file);
  fclose(file);
  file_contents[read_size] = '\0';

  /* Parse JSON to validate structure */
  cJSON *root = cJSON_Parse(file_contents);
  free(file_contents);

  if (!root)
    return CONFIG_ERROR_PARSE_FAILED;

  /* Check for required top-level objects */
  cJSON *global_state = cJSON_GetObjectItemCaseSensitive(root, "global_state");
  cJSON *memory_loops = cJSON_GetObjectItemCaseSensitive(root, "memory_loops");

  config_result_t result = CONFIG_SUCCESS;
  if (!global_state || !cJSON_IsObject(global_state))
  {
    result = CONFIG_ERROR_INVALID_DATA;
  }
  else if (!memory_loops || !cJSON_IsArray(memory_loops))
  {
    result = CONFIG_ERROR_INVALID_DATA;
  }

  cJSON_Delete(root);
  return result;
}

const char *config_get_error_message(config_result_t result)
{
  switch (result)
  {
  case CONFIG_SUCCESS:
    return "Success";
  case CONFIG_ERROR_FILE_NOT_FOUND:
    return "Configuration file not found";
  case CONFIG_ERROR_PARSE_FAILED:
    return "Failed to parse configuration file";
  case CONFIG_ERROR_WRITE_FAILED:
    return "Failed to write configuration file";
  case CONFIG_ERROR_INVALID_VERSION:
    return "Invalid configuration file version";
  case CONFIG_ERROR_MEMORY:
    return "Memory allocation error";
  case CONFIG_ERROR_INVALID_DATA:
    return "Invalid configuration data";
  default:
    return "Unknown error";
  }
}

config_result_t config_create_backup(struct data *data, const char *backup_filename)
{
  char filename[512];
  if (backup_filename)
  {
    strncpy(filename, backup_filename, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
  }
  else
  {
    if (!config_generate_backup_filename(filename, sizeof(filename)))
    {
      return CONFIG_ERROR_INVALID_DATA;
    }
  }

  return config_save_state(data, filename);
}

void config_reset_to_defaults(struct data *data)
{
  if (!data)
    return;

  /* Reset global settings to defaults */
  data->volume = 1.0f;
  data->playback_speed = 1.0f;
  data->pitch_shift = 0.0f;
  data->rubberband_enabled = false;
  data->current_state = HOLO_STATE_IDLE;
  data->current_playback_mode = PLAYBACK_MODE_TRIGGER;

  /* Reset sync mode settings */
  data->sync_mode_enabled = false;
  data->pulse_loop_note = 255;
  data->pulse_loop_duration = 0;
  data->sync_cutoff_percentage = 0.5f;
  data->sync_recording_cutoff_percentage = 0.5f;

  /* Reset loop management */
  data->active_loop_count = 0;
  data->currently_recording_note = 255;

  /* Reset all memory loops to idle state */
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->buffer)
    {
      loop->recorded_frames = 0;
      loop->playback_position = 0;
      loop->loop_ready = false;
      loop->recording_to_memory = false;
      loop->is_playing = false;
      loop->pending_record = false;
      loop->pending_stop = false;
      loop->pending_start = false;
      loop->current_state = LOOP_STATE_IDLE;
      loop->volume = 1.0f;
      memset(loop->loop_filename, 0, sizeof(loop->loop_filename));
    }
  }
}

bool config_get_config_dir(char *buffer, size_t buffer_size)
{
  const char *home = getenv("HOME");
  if (!home)
    return false;

  int result = snprintf(buffer, buffer_size, "%s/.config/uphonor", home);
  if (result < 0 || (size_t)result >= buffer_size)
    return false;

  /* Create directory if it doesn't exist */
  struct stat st = {0};
  if (stat(buffer, &st) == -1)
  {
    if (mkdir(buffer, 0755) != 0)
    {
      return false;
    }
  }

  return true;
}

bool config_generate_backup_filename(char *buffer, size_t buffer_size)
{
  char config_dir[512];
  if (!config_get_config_dir(config_dir, sizeof(config_dir)))
  {
    return false;
  }

  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);

  int result = snprintf(buffer, buffer_size, "%s/uphonor_backup_%04d%02d%02d_%02d%02d%02d.json",
                        config_dir,
                        tm_info->tm_year + 1900,
                        tm_info->tm_mon + 1,
                        tm_info->tm_mday,
                        tm_info->tm_hour,
                        tm_info->tm_min,
                        tm_info->tm_sec);

  return (result > 0 && (size_t)result < buffer_size);
}
