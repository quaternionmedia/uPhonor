#include "uphonor.h"

int start_playing(struct data *data, const char *filename)
{
  if (!filename)
  {
    pw_log_error("start_playing called with NULL filename");
    return -1;
  }

  data->current_state = HOLO_STATE_PLAYING;
  memset(&data->fileinfo, 0, sizeof(data->fileinfo));

  data->file = sf_open(filename, SFM_READ, &data->fileinfo);
  if (data->file == NULL)
  {
    pw_log_error("failed to open file: %s", filename);
    return -1;
  }

  /* Initialize or reset rubberband when loading a new file */
  if (data->rubberband_state) {
    rubberband_reset_data(data);
  } else if (data->rubberband_enabled) {
    /* Set up format info so rubberband can initialize properly */
    if (data->format.info.raw.rate == 0) {
      data->format.info.raw.rate = data->fileinfo.samplerate;
      data->format.info.raw.channels = 1; /* We convert to mono */
    }
    
    /* Initialize rubberband with file sample rate */
    pw_log_info("DEBUG: Initializing rubberband with file sample rate %d", data->fileinfo.samplerate);
    if (init_rubberband(data) == 0) {
      pw_log_info("Rubberband initialized successfully with file format");
      pw_log_info("DEBUG: Rubberband initialized successfully, state: %p", (void*)data->rubberband_state);
    } else {
      pw_log_warn("Failed to initialize rubberband with file format");
      pw_log_warn("DEBUG: Rubberband initialization FAILED");
    }
  }

  return 0;
}
