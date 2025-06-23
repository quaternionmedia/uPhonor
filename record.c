#include "uphonor.h"
#include <sys/stat.h>

/* Function to start recording */
int start_recording(struct data *data, const char *filename)
{
  if (data->recording_enabled)
  {
    pw_log_warn("Recording already in progress");
    return -1;
  }

  // Set up recording file info
  data->record_fileinfo.samplerate = 48000; // Use default sample rate
  data->record_fileinfo.channels = 1;       // Mono recording
  data->record_fileinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

  // Generate filename if not provided
  char default_filename[256];
  if (!filename)
  {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(default_filename, sizeof(default_filename), "recording_%Y%m%d_%H%M%S.wav", tm_info);
    filename = default_filename;
  }

  // Create recordings directory if it doesn't exist
  struct stat st = {0};
  if (stat("recordings", &st) == -1)
  {
    mkdir("recordings", 0755);
  }

  // Construct full path
  char full_path[512];
  snprintf(full_path, sizeof(full_path), "recordings/%s", filename);

  // Open recording file
  data->record_file = sf_open(full_path, SFM_WRITE, &data->record_fileinfo);
  if (!data->record_file)
  {
    pw_log_error("Could not open recording file: %s", sf_strerror(NULL));
    return -1;
  }

  data->recording_enabled = true;
  data->record_filename = strdup(full_path);

  pw_log_info("Started recording to: %s", full_path);
  return 0;
}

/* Function to stop recording */
int stop_recording(struct data *data)
{
  if (!data->recording_enabled)
  {
    pw_log_warn("No recording in progress");
    return -1;
  }

  if (data->record_file)
  {
    sf_close(data->record_file);
    data->record_file = NULL;
  }

  if (data->record_filename)
  {
    pw_log_info("Stopped recording to: %s", data->record_filename);
    free(data->record_filename);
    data->record_filename = NULL;
  }

  data->recording_enabled = false;
  return 0;
}