#include "loop_manager.h"
#include "uphonor.h"
#include "audio_processing_rt.h"
#include <string.h>
#include <math.h>
#include <pipewire/pipewire.h>

void process_multiple_loops_audio_output(struct data *data, struct spa_io_position *position)
{
  if (!data || !data->loop_mgr || !position) return;
  
  struct pw_buffer *b;
  uint32_t n_samples = position->clock.duration;
  float *output_buf;
  
  /* Get output buffer */
  if ((b = pw_filter_dequeue_buffer(data->audio_out)) == NULL) {
    return;
  }
  
  if (b->buffer->n_datas < 1) {
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }
  
  struct spa_data *d = &b->buffer->datas[0];
  uint32_t stride = sizeof(float);
  output_buf = (float*)d->data;
  
  if (!output_buf) {
    pw_filter_queue_buffer(data->audio_out, b);
    return;
  }
  
  /* Clear output buffer */
  memset(output_buf, 0, n_samples * stride);
  
  /* Mix all active playing loops */
  int active_loops = 0;
  for (int i = 0; i < MAX_LOOPS; i++) {
    struct loop_slot *loop = &data->loop_mgr->loops[i];
    
    if (!loop->active || loop->state != HOLO_STATE_PLAYING || !loop->file) {
      continue;
    }
    
    /* Reset audio if needed for this loop */
    if (loop->reset_audio) {
      sf_seek(loop->file, 0, SEEK_SET);
      loop->sample_position = 0.0;
      loop->reset_audio = false;
      audio_buffer_rt_reset(&loop->audio_buffer);
      audio_buffer_rt_fill(&loop->audio_buffer, loop->file, &loop->fileinfo);
    }
    
    /* Read audio from this loop using RT-optimized buffering */
    float temp_buf[n_samples];
    sf_count_t frames_read = audio_buffer_rt_read(&loop->audio_buffer, 
                                                 loop->file, 
                                                 &loop->fileinfo,
                                                 temp_buf, 
                                                 n_samples);
    
    /* Apply volume and mix into output */
    float loop_volume = loop->volume * data->loop_mgr->master_volume;
    for (uint32_t j = 0; j < frames_read; j++) {
      output_buf[j] += temp_buf[j] * loop_volume;
    }
    
    active_loops++;
  }
  
  /* Normalize output if multiple loops are playing */
  if (active_loops > 1) {
    float norm_factor = 1.0f / sqrtf((float)active_loops);
    for (uint32_t i = 0; i < n_samples; i++) {
      output_buf[i] *= norm_factor;
    }
  }
  
  /* Set buffer metadata */
  b->buffer->datas[0].chunk->offset = 0;
  b->buffer->datas[0].chunk->stride = stride;
  b->buffer->datas[0].chunk->size = n_samples * stride;
  
  pw_filter_queue_buffer(data->audio_out, b);
}

void handle_multiple_loops_audio_input(struct data *data, uint32_t n_samples)
{
  if (!data || !data->loop_mgr) return;
  
  /* Get input buffer */
  float *input_buf = pw_filter_get_dsp_buffer(data->audio_in, n_samples);
  
  /* Handle recording for all loops that are in recording state */
  for (int i = 0; i < MAX_LOOPS; i++) {
    struct loop_slot *loop = &data->loop_mgr->loops[i];
    
    if (!loop->active || loop->state != HOLO_STATE_RECORDING || !loop->record_file) {
      continue;
    }
    
    /* Write input to this loop's recording file */
    sf_count_t frames_written;
    if (input_buf == NULL) {
      /* Use silence if no input */
      frames_written = sf_writef_float(loop->record_file, 
                                      data->silence_buffer, 
                                      n_samples);
    } else {
      /* Write input data */
      frames_written = sf_writef_float(loop->record_file, input_buf, n_samples);
    }
    
    if (frames_written != n_samples) {
      pw_log_warn("Could not write all frames for loop %d: wrote %ld of %d",
                  i, frames_written, n_samples);
    }
    
    /* Sync to disk periodically */
    static int sync_counter = 0;
    if (++sync_counter >= 500) { /* ~10 seconds at 48kHz/1024 */
      sf_write_sync(loop->record_file);
      sync_counter = 0;
    }
  }
}
