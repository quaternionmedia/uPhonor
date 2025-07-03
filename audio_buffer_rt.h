#ifndef AUDIO_BUFFER_RT_H
#define AUDIO_BUFFER_RT_H

#include <stdint.h>
#include <stdbool.h>
#include <sndfile.h>

/* RT-safe audio buffer management for minimizing file I/O in audio callback */

#define AUDIO_BUFFER_SIZE 8192 /* Large buffer to reduce file I/O frequency */

struct audio_buffer_rt
{
    float *buffer;            /* Audio data buffer */
    uint32_t size;            /* Total buffer size in samples */
    uint32_t valid_samples;   /* Number of valid samples currently in buffer */
    uint32_t read_position;   /* Current read position in buffer */
    sf_count_t file_position; /* Position in the source file */
    bool loop_mode;           /* Whether to loop the audio file */

    /* Pre-allocated temp buffer for multi-channel reading */
    float *temp_multichannel;
    uint32_t temp_size;
};

/* Initialize the audio buffer system */
int audio_buffer_rt_init(struct audio_buffer_rt *ab, uint32_t channels);

/* Cleanup the audio buffer system */
void audio_buffer_rt_cleanup(struct audio_buffer_rt *ab);

/* Fill buffer from audio file (call this from non-RT thread or during initialization) */
int audio_buffer_rt_fill(struct audio_buffer_rt *ab, SNDFILE *file, SF_INFO *fileinfo);

/* Read samples from buffer (RT-safe, minimal file I/O) */
sf_count_t audio_buffer_rt_read(struct audio_buffer_rt *ab, SNDFILE *file, SF_INFO *fileinfo,
                                float *output, uint32_t n_samples);

/* Reset buffer to beginning (RT-safe) */
void audio_buffer_rt_reset(struct audio_buffer_rt *ab);

/* Check if buffer needs refilling (RT-safe) */
bool audio_buffer_rt_needs_refill(const struct audio_buffer_rt *ab);

#endif /* AUDIO_BUFFER_RT_H */
