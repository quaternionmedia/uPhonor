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
  return 0;
}
