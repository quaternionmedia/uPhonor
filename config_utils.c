#include "config.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* High-level configuration management functions for integration with the main application */

/**
 * Save current session with user-friendly feedback
 */
int save_current_session(struct data *data, const char *session_name)
{
  char filename[512];

  if (session_name && strlen(session_name) > 0)
  {
    if (strstr(session_name, ".json") == NULL)
    {
      snprintf(filename, sizeof(filename), "%s.json", session_name);
    }
    else
    {
      strncpy(filename, session_name, sizeof(filename) - 1);
      filename[sizeof(filename) - 1] = '\0';
    }
  }
  else
  {
    strncpy(filename, DEFAULT_CONFIG_FILENAME, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
  }

  printf("Saving session to: %s\n", filename);

  config_result_t result = config_save_state(data, filename);
  if (result == CONFIG_SUCCESS)
  {
    printf("Session saved successfully!\n");
    return 0;
  }
  else
  {
    printf("Error saving session: %s\n", config_get_error_message(result));
    return -1;
  }
}

/**
 * Load session with user-friendly feedback and backup creation
 */
int load_session(struct data *data, const char *session_name)
{
  char filename[512];

  if (session_name && strlen(session_name) > 0)
  {
    if (strstr(session_name, ".json") == NULL)
    {
      snprintf(filename, sizeof(filename), "%s.json", session_name);
    }
    else
    {
      strncpy(filename, session_name, sizeof(filename) - 1);
      filename[sizeof(filename) - 1] = '\0';
    }
  }
  else
  {
    strncpy(filename, DEFAULT_CONFIG_FILENAME, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
  }

  printf("Loading session from: %s\n", filename);

  /* Validate the file first */
  config_result_t validation = config_validate_file(filename);
  if (validation != CONFIG_SUCCESS)
  {
    printf("Error: Invalid configuration file - %s\n", config_get_error_message(validation));
    return -1;
  }

  /* Create backup of current state before loading */
  printf("Creating backup of current state...\n");
  config_result_t backup_result = config_create_backup(data, NULL);
  if (backup_result == CONFIG_SUCCESS)
  {
    printf("Backup created successfully.\n");
  }
  else
  {
    printf("Warning: Could not create backup - %s\n", config_get_error_message(backup_result));
    printf("Continue loading? (y/N): ");
    char response[16];
    if (fgets(response, sizeof(response), stdin) == NULL ||
        (response[0] != 'y' && response[0] != 'Y'))
    {
      printf("Load cancelled.\n");
      return -1;
    }
  }

  /* Load the configuration */
  config_result_t result = config_load_state(data, filename);
  if (result == CONFIG_SUCCESS)
  {
    printf("Session configuration loaded successfully!\n");

    /* Now load the actual audio files */
    printf("\nLoading audio files...\n");
    int audio_files_loaded = config_load_audio_files(data);

    if (audio_files_loaded > 0)
    {
      printf("Successfully loaded %d audio files!\n", audio_files_loaded);
    }
    else if (audio_files_loaded == 0)
    {
      printf("No audio files were loaded. Configuration contains metadata only.\n");
    }
    else
    {
      printf("Error occurred while loading audio files.\n");
    }

    /* Print summary of loaded state */
    printf("\nLoaded session summary:\n");
    printf("- Volume: %.2f\n", data->volume);
    printf("- Playback speed: %.2f\n", data->playback_speed);
    printf("- Playback mode: %s\n",
           data->current_playback_mode == PLAYBACK_MODE_NORMAL ? "NORMAL" : "TRIGGER");
    printf("- Sync mode: %s\n", data->sync_mode_enabled ? "ENABLED" : "DISABLED");

    if (data->sync_mode_enabled && data->pulse_loop_note != 255)
    {
      printf("- Pulse loop: Note %d\n", data->pulse_loop_note);
    }

    int configured_loops = 0;
    int ready_loops = 0;
    for (int i = 0; i < 128; i++)
    {
      if (data->memory_loops[i].recorded_frames > 0 ||
          strlen(data->memory_loops[i].loop_filename) > 0)
      {
        configured_loops++;
        if (data->memory_loops[i].loop_ready)
        {
          ready_loops++;
        }
      }
    }
    printf("- Configured loop slots: %d\n", configured_loops);
    printf("- Ready loops with audio: %d\n", ready_loops);

    if (configured_loops > ready_loops)
    {
      printf("\nNote: %d loop slots were configured but could not load audio data.\n",
             configured_loops - ready_loops);
      printf("Check that the audio files exist in the 'recordings' directory.\n");
    }

    return 0;
  }
  else
  {
    printf("Error loading session: %s\n", config_get_error_message(result));
    return -1;
  }
}

/**
 * Save only the loops that have recorded content
 */
int save_active_loops(struct data *data, const char *session_name)
{
  char filename[512];

  if (session_name && strlen(session_name) > 0)
  {
    if (strstr(session_name, ".json") == NULL)
    {
      snprintf(filename, sizeof(filename), "%s_active.json", session_name);
    }
    else
    {
      /* Replace .json with _active.json */
      strncpy(filename, session_name, sizeof(filename) - 1);
      filename[sizeof(filename) - 1] = '\0';
      char *dot = strrchr(filename, '.');
      if (dot)
      {
        strcpy(dot, "_active.json");
      }
      else
      {
        strncat(filename, "_active.json", sizeof(filename) - strlen(filename) - 1);
      }
    }
  }
  else
  {
    strncpy(filename, "uphonor_active_loops.json", sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
  }

  printf("Saving active loops to: %s\n", filename);

  config_result_t result = config_save_active_loops_only(data, filename);
  if (result == CONFIG_SUCCESS)
  {
    /* Count active loops */
    int active_count = 0;
    for (int i = 0; i < 128; i++)
    {
      if (data->memory_loops[i].loop_ready && data->memory_loops[i].recorded_frames > 0)
      {
        active_count++;
      }
    }
    printf("Successfully saved %d active loops!\n", active_count);
    return 0;
  }
  else
  {
    printf("Error saving active loops: %s\n", config_get_error_message(result));
    return -1;
  }
}

/**
 * Reset all loops and settings to defaults
 */
int reset_to_defaults(struct data *data)
{
  printf("Resetting all settings and loops to defaults...\n");

  /* Create backup first */
  printf("Creating backup before reset...\n");
  config_result_t backup_result = config_create_backup(data, NULL);
  if (backup_result != CONFIG_SUCCESS)
  {
    printf("Warning: Could not create backup - %s\n", config_get_error_message(backup_result));
    printf("Continue with reset? (y/N): ");
    char response[16];
    if (fgets(response, sizeof(response), stdin) == NULL ||
        (response[0] != 'y' && response[0] != 'Y'))
    {
      printf("Reset cancelled.\n");
      return -1;
    }
  }

  config_reset_to_defaults(data);
  printf("Reset to defaults completed successfully!\n");
  return 0;
}

/**
 * List available configuration files
 */
void list_available_sessions(void)
{
  char config_dir[512];
  if (!config_get_config_dir(config_dir, sizeof(config_dir)))
  {
    printf("Could not access configuration directory.\n");
    return;
  }

  printf("Available session files:\n");
  printf("Configuration directory: %s\n\n", config_dir);

  /* Check for default session file */
  if (access(DEFAULT_CONFIG_FILENAME, R_OK) == 0)
  {
    printf("- %s (default session)\n", DEFAULT_CONFIG_FILENAME);
  }

  /* This is a simple implementation - in a full version you might want to */
  /* use opendir/readdir to scan the config directory for .json files */
  printf("\nTo list all session files in the config directory, use:\n");
  printf("ls %s/*.json\n", config_dir);
}

/**
 * Print configuration status
 */
void print_config_status(struct data *data)
{
  printf("\n=== uPhonor Configuration Status ===\n");
  printf("Volume: %.2f\n", data->volume);
  printf("Playback Speed: %.2f\n", data->playback_speed);
  printf("Pitch Shift: %.2f semitones\n", data->pitch_shift);
  printf("Rubberband: %s\n", data->rubberband_enabled ? "ENABLED" : "DISABLED");
  printf("Current State: %s\n",
         data->current_state == HOLO_STATE_IDLE ? "IDLE" : data->current_state == HOLO_STATE_PLAYING ? "PLAYING"
                                                                                                     : "STOPPED");
  printf("Playback Mode: %s\n",
         data->current_playback_mode == PLAYBACK_MODE_NORMAL ? "NORMAL" : "TRIGGER");

  printf("\n--- Sync Settings ---\n");
  printf("Sync Mode: %s\n", data->sync_mode_enabled ? "ENABLED" : "DISABLED");
  if (data->sync_mode_enabled)
  {
    printf("Pulse Loop Note: %s\n",
           data->pulse_loop_note == 255 ? "None" : "");
    if (data->pulse_loop_note != 255)
    {
      printf("%d\n", data->pulse_loop_note);
    }
    printf("Pulse Loop Duration: %u frames\n", data->pulse_loop_duration);
    printf("Sync Cutoff: %.1f%%\n", data->sync_cutoff_percentage * 100);
    printf("Recording Cutoff: %.1f%%\n", data->sync_recording_cutoff_percentage * 100);
  }

  printf("\n--- Loop Status ---\n");
  printf("Active Loop Count: %d\n", data->active_loop_count);
  printf("Currently Recording: %s\n",
         data->currently_recording_note == 255 ? "None" : "");
  if (data->currently_recording_note != 255)
  {
    printf("Note %d\n", data->currently_recording_note);
  }

  int total_ready = 0, total_playing = 0, total_recording = 0;
  for (int i = 0; i < 128; i++)
  {
    struct memory_loop *loop = &data->memory_loops[i];
    if (loop->loop_ready)
      total_ready++;
    if (loop->is_playing)
      total_playing++;
    if (loop->current_state == LOOP_STATE_RECORDING)
      total_recording++;
  }

  printf("Ready Loops: %d\n", total_ready);
  printf("Playing Loops: %d\n", total_playing);
  printf("Recording Loops: %d\n", total_recording);
  printf("=====================================\n\n");
}
