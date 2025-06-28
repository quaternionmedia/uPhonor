#include "uphonor.h"
#include "audio_processing_rt.h"

/* Recording and playback functions - now using RT-safe bridge */
int start_recording(struct data *data, const char *filename)
{
  return start_recording_rt(data, filename);
}

int stop_recording(struct data *data)
{
  return stop_recording_rt(data);
}