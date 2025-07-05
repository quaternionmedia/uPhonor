#include "midi_processing.h"

#define PERIOD_NSEC (SPA_NSEC_PER_SEC / 8)
#define SPEED_CC_NUMBER 74                 /* MIDI CC 74 for playback speed control */
#define PITCH_CC_NUMBER 75                 /* MIDI CC 75 for pitch shift control */
#define RECORD_PLAYER_CC_NUMBER 76         /* MIDI CC 76 for record player mode */
#define VOLUME_CC_NUMBER 7                 /* MIDI CC 7 for volume control */
#define PLAYBACK_MODE_CC_NUMBER 77         /* MIDI CC 77 for playback mode (normal/trigger) */
#define SYNC_MODE_CC_NUMBER 78             /* MIDI CC 78 for sync mode on/off */
#define SYNC_CUTOFF_CC_NUMBER 79           /* MIDI CC 79 for sync playback cutoff point (0-100% of pulse duration) */
#define SYNC_RECORDING_CUTOFF_CC_NUMBER 80 /* MIDI CC 78 for sync recording cutoff point (0-100% of pulse duration) */

void handle_midi_message(struct data *data, uint8_t *midi_data)
{
  uint8_t message_type = *midi_data & 0xf0;
  uint8_t channel = *midi_data & 0x0f;

  switch (message_type)
  {
  case 0x80: // Note Off
    pw_log_debug("Note Off message received: 0x%02x", *midi_data);
    // Note: MIDI messages are typically 3 bytes for note on/off
    {
      uint8_t note = *(midi_data + 1);
      uint8_t velocity = *(midi_data + 2);
      handle_note_off(data, channel, note, velocity);
    }
    break;

  case 0x90: // Note On
    pw_log_info("Note On message received: 0x%02x", *midi_data);
    // Note: MIDI messages are typically 3 bytes for note on/off
    {
      uint8_t note = *(midi_data + 1);
      uint8_t velocity = *(midi_data + 2);
      handle_note_on(data, channel, note, velocity);
    }
    break;

  case 0xA0: // Polyphonic Aftertouch
    pw_log_debug("Polyphonic Aftertouch message received: 0x%02x", *midi_data);
    break;

  case 0xB0: // Control Change
    pw_log_debug("Control Change message received: 0x%02x", *midi_data);
    {
      uint8_t controller = *(midi_data + 1);
      uint8_t value = *(midi_data + 2);
      handle_control_change(data, channel, controller, value);
    }
    break;

  case 0xC0: // Program Change
    pw_log_debug("Program Change message received: 0x%02x", *midi_data);
    break;

  case 0xD0: // Channel Pressure
    pw_log_debug("Channel Pressure message received: 0x%02x", *midi_data);
    break;

  case 0xE0: // Pitch Bend
    pw_log_debug("Pitch Bend message received: 0x%02x", *midi_data);
    break;

  case 0xF0: // System messages
    switch (*midi_data)
    {
    case 0xF8:
      pw_log_debug("Timing Clock message received");
      break;
    case 0xFA:
      pw_log_debug("Start message received");
      break;
    case 0xFB:
      pw_log_debug("Continue message received");
      break;
    case 0xFC:
      pw_log_debug("Stop message received");
      break;
    case 0xFE:
      pw_log_debug("Active Sensing message received");
      break;
    case 0xFF:
      pw_log_debug("System Reset message received");
      break;
    default:
      if ((*midi_data & 0xF0) == 0xF0)
        pw_log_debug("System Exclusive message received");
      break;
    }
    break;

  default:
    pw_log_trace("Unknown MIDI message type: 0x%02x", *midi_data);
    break;
  }
}

void handle_note_on(struct data *data, uint8_t channel, uint8_t note, uint8_t velocity)
{
  // Convert MIDI velocity to volume (0.0-1.0)
  float volume = (float)(velocity & 0x7f) / 127.0f;

  pw_log_info("Note On: channel=%d, note=%d, velocity=%d, volume=%.2f, mode=%s, sync=%s",
              channel, note, velocity, volume, get_playback_mode_name(data),
              is_sync_mode_enabled(data) ? "ON" : "OFF");

  // Check sync mode constraints before processing
  if (is_sync_mode_enabled(data))
  {
    struct memory_loop *loop = get_loop_by_note(data, note);
    if (!loop)
    {
      pw_log_error("Failed to get loop for note %d", note);
      return;
    }

    // If this loop doesn't have content and we want to record
    if (!loop->loop_ready || loop->recorded_frames == 0)
    {
      // This is a new recording request in sync mode
      pw_log_info("SYNC mode: Marking note %d for pending recording", note);
      loop->volume = volume;
      process_loops(data, NULL, note, volume);
      return; // Exit early - don't go through normal playback mode logic
    }
    // If loop has content, continue to normal playback mode logic below
  }

  if (data->current_playback_mode == PLAYBACK_MODE_NORMAL)
  {
    // NORMAL mode: Note On toggles between play and stop
    struct memory_loop *loop = get_loop_by_note(data, note);
    if (!loop)
    {
      pw_log_error("Failed to get loop for note %d", note);
      return;
    }

    // Set volume for this loop
    loop->volume = volume;

    if (loop->current_state == LOOP_STATE_PLAYING)
    {
      // Currently playing, so stop it
      pw_log_info("NORMAL mode: Stopping playback for note %d", note);
      loop->current_state = LOOP_STATE_STOPPED;
      loop->is_playing = false;

      // In sync mode, clear any pending record flag
      if (data->sync_mode_enabled)
      {
        loop->pending_record = false;
      }
    }
    else if (loop->loop_ready && loop->recorded_frames > 0)
    {
      // Has content and not playing, so start it
      pw_log_info("NORMAL mode: Starting playback for note %d", note);
      loop->current_state = LOOP_STATE_PLAYING;
      loop->pending_start = false; // Clear any pending start from previous state

      // Calculate synchronized start position in sync mode
      if (data->sync_mode_enabled && data->pulse_loop_note != 255)
      {
        // Get the pulse loop to sync with
        struct memory_loop *pulse_loop = get_loop_by_note(data, data->pulse_loop_note);
        if (pulse_loop && pulse_loop->is_playing && data->pulse_loop_duration > 0)
        {
          // Calculate current pulse position and cutoff point
          uint32_t pulse_position = pulse_loop->playback_position;
          uint32_t cutoff_position = (uint32_t)(data->sync_cutoff_percentage * data->pulse_loop_duration);

          // Decide whether to sync to current pulse or wait for next
          if (pulse_position <= cutoff_position)
          {
            // Before cutoff - sync to current pulse position and start playing
            if (pulse_position < loop->recorded_frames)
            {
              // Pulse position fits within this loop - start there
              loop->playback_position = pulse_position;
            }
            else
            {
              // Pulse position is beyond this loop's length - use modulo
              loop->playback_position = pulse_position % loop->recorded_frames;
            }

            loop->is_playing = true;

            pw_log_info("SYNC mode: Starting loop %d at current pulse position %u (pulse at %u, cutoff at %u)",
                        note, loop->playback_position, pulse_position, cutoff_position);
          }
          else
          {
            // After cutoff - mark as pending start and wait for next pulse cycle
            loop->playback_position = 0;
            loop->is_playing = false;
            loop->pending_start = true;

            pw_log_info("SYNC mode: Loop %d marked as pending start - waiting for next pulse cycle (pulse at %u, cutoff at %u)",
                        note, pulse_position, cutoff_position);
          }
        }
        else
        {
          // No pulse loop or pulse not playing - start immediately
          loop->playback_position = 0;
          loop->is_playing = true;
          pw_log_info("SYNC mode: No active pulse loop, starting loop %d from beginning", note);
        }
      }
      else
      {
        // Not in sync mode - start playing immediately
        loop->playback_position = 0;
        loop->is_playing = true;
      }

      loop->pending_record = false;
    }
    else
    {
      // No content yet, start recording or process normally
      process_loops(data, NULL, note, volume);
    }
  }
  else
  {
    // TRIGGER mode: Note On starts playback (current behavior)
    process_loops(data, NULL, note, volume);
  }

  // Reset audio only when sync mode is disabled or no sync coordination is needed
  if (!data->sync_mode_enabled)
  {
    data->reset_audio = true;
  }
}

void handle_note_off(struct data *data, uint8_t channel, uint8_t note, uint8_t velocity)
{
  pw_log_info("Note Off: channel=%d, note=%d, velocity=%d, mode=%s, sync=%s",
              channel, note, velocity, get_playback_mode_name(data),
              is_sync_mode_enabled(data) ? "ON" : "OFF");

  if (data->current_playback_mode == PLAYBACK_MODE_NORMAL)
  {
    // NORMAL mode: Note Off messages are ignored
    pw_log_info("NORMAL mode: Ignoring Note Off for note %d", note);
    return;
  }

  // TRIGGER mode: Note Off stops both playback and recording
  struct memory_loop *loop = get_loop_by_note(data, note);
  if (!loop)
  {
    pw_log_error("Failed to get loop for note %d", note);
    return;
  }

  if (loop->current_state == LOOP_STATE_PLAYING)
  {
    pw_log_info("TRIGGER mode: Stopping playback for note %d", note);
    loop->current_state = LOOP_STATE_STOPPED;
    loop->is_playing = false;
  }
  else if (loop->current_state == LOOP_STATE_RECORDING)
  {
    // Handle sync mode differently - don't stop immediately
    if (is_sync_mode_enabled(data))
    {
      // In sync mode, mark for stopping at next pulse reset instead of stopping immediately
      loop->pending_stop = true;
      pw_log_info("SYNC mode: Marking recording for note %d to stop at next pulse reset", note);
      return; // Don't stop immediately
    }

    pw_log_info("TRIGGER mode: Stopping recording for note %d", note);
    stop_loop_recording_rt(data, note);

    /* Give the system a moment to process the stop command */
    usleep(1000); // 1ms

    loop->current_state = LOOP_STATE_STOPPED;
    loop->is_playing = false;
    data->currently_recording_note = 255; // No longer recording

    // Handle sync mode logic if enabled (this case is for non-sync mode)
    if (is_sync_mode_enabled(data))
    {
      // Update pulse loop duration if this is the pulse loop
      if (note == data->pulse_loop_note)
      {
        data->pulse_loop_duration = loop->recorded_frames;
        data->waiting_for_pulse_reset = true; // Prevent new recordings until reset
        pw_log_info("SYNC mode: Pulse loop recorded with %u frames", data->pulse_loop_duration);
      }
      else
      {
        // For non-pulse loops, check if duration is a multiple of pulse duration
        if (data->pulse_loop_duration > 0)
        {
          uint32_t multiple = (loop->recorded_frames + data->pulse_loop_duration / 2) / data->pulse_loop_duration;
          if (multiple == 0)
            multiple = 1;
          uint32_t target_duration = multiple * data->pulse_loop_duration;

          // Adjust loop duration to be exactly a multiple of pulse duration
          if (loop->recorded_frames != target_duration)
          {
            loop->recorded_frames = target_duration;
            pw_log_info("SYNC mode: Adjusted loop duration to %u frames (%ux pulse)",
                        target_duration, multiple);
          }
        }
      }
    }

    pw_log_info("TRIGGER mode: Recording stopped for note %d, ready for playback on next Note On", note);
  }
}

void handle_control_change(struct data *data, uint8_t channel, uint8_t controller, uint8_t value)
{
  switch (controller)
  {
  case SPEED_CC_NUMBER:
  {
    /* Convert MIDI CC value (0-127) to playback speed (0.25x to 4.0x) */
    /* CC value 64 = normal speed (1.0x)
     * CC value 0 = quarter speed (0.25x)
     * CC value 127 = quadruple speed (4.0x)
     */
    float new_speed;

    /* Map CC value to speed with center detent at 64 */
    if (value < 64)
    {
      /* Map 0-63 to 0.25-1.0 */
      new_speed = 0.25f + ((float)value / 63.0f) * 0.75f;
    }
    else
    {
      /* Map 64-127 to 1.0-4.0 */
      new_speed = 1.0f + ((float)(value - 64) / 63.0f) * 3.0f;
    }

    /* Use the proper speed setting function that handles rubberband */
    set_playback_speed(data, new_speed);

    /* Auto-enable rubberband for speed changes (preserve pitch) */
    if (new_speed != 1.0f)
    {
      set_rubberband_enabled(data, true);
      pw_log_info("MIDI CC%d: Speed %.2fx (rubberband auto-enabled)", controller, new_speed);
    }
    else
    {
      pw_log_info("MIDI CC%d: Speed %.2fx (normal)", controller, new_speed);
    }
  }
  break;

  case PITCH_CC_NUMBER:
  {
    /* Convert MIDI CC value (0-127) to pitch shift (-12 to +12 semitones) */
    /* CC value 64 = no pitch shift (0 semitones)
     * CC value 0 = -12 semitones (one octave down)
     * CC value 127 = +12 semitones (one octave up)
     */
    float pitch_shift;

    /* Map CC value to pitch shift with center detent at 64 */
    if (value < 64)
    {
      /* Map 0-63 to -12.0 to 0.0 semitones */
      pitch_shift = -12.0f + ((float)value / 63.0f) * 12.0f;
    }
    else
    {
      /* Map 64-127 to 0.0 to +12.0 semitones */
      pitch_shift = ((float)(value - 64) / 63.0f) * 12.0f;
    }

    /* Use the pitch shift function */
    set_pitch_shift(data, pitch_shift);

    /* Auto-enable rubberband for pitch changes */
    if (pitch_shift != 0.0f)
    {
      set_rubberband_enabled(data, true);
      pw_log_info("MIDI CC%d: Pitch shift %.2f semitones (rubberband auto-enabled)", controller, pitch_shift);
    }
    else
    {
      pw_log_info("MIDI CC%d: Pitch shift %.2f semitones (normal)", controller, pitch_shift);
    }
  }
  break;

  case RECORD_PLAYER_CC_NUMBER:
  {
    /* Convert MIDI CC value (0-127) to record player speed/pitch factor (0.25x to 4.0x) */
    /* CC value 64 = normal speed/pitch (1.0x)
     * CC value 0 = quarter speed/pitch (0.25x)
     * CC value 127 = quadruple speed/pitch (4.0x)
     */
    float speed_pitch_factor;

    /* Map CC value to speed/pitch factor with center detent at 64 */
    if (value < 64)
    {
      /* Map 0-63 to 0.25-1.0 */
      speed_pitch_factor = 0.25f + ((float)value / 63.0f) * 0.75f;
    }
    else
    {
      /* Map 64-127 to 1.0-4.0 */
      speed_pitch_factor = 1.0f + ((float)(value - 64) / 63.0f) * 3.0f;
    }

    /* Set record player mode (disables rubberband, links speed and pitch) */
    set_record_player_mode(data, speed_pitch_factor);

    pw_log_info("MIDI CC%d: Record player mode %.2fx speed/pitch", controller, speed_pitch_factor);
  }
  break;

  case VOLUME_CC_NUMBER:
  {
    /* Convert MIDI CC value (0-127) to volume (0.0-1.0) */
    float volume = (float)(value & 0x7f) / 127.0f;

    /* Set the volume */
    set_volume(data, volume);

    pw_log_info("MIDI CC%d: Volume set to %.2f", controller, volume);
  }
  break;

  case PLAYBACK_MODE_CC_NUMBER:
  {
    /* Set playback mode based on value ranges */
    if (value >= 64)
    {
      /* High values (64-127) = TRIGGER mode */
      set_playback_mode_trigger(data);
    }
    else if (value > 0)
    {
      /* Low values (1-63) = NORMAL mode */
      set_playback_mode_normal(data);
    }
    else
    {
      /* Value 0 = toggle mode */
      toggle_playback_mode(data);
    }

    pw_log_info("MIDI CC%d: Playback mode set to %s (value=%d)",
                controller, get_playback_mode_name(data), value);
  }
  break;

  case SYNC_MODE_CC_NUMBER:
  {
    /* Toggle sync mode based on value */
    if (value >= 64)
    {
      /* High values (64-127) = Enable sync mode */
      enable_sync_mode(data);
    }
    else if (value > 0)
    {
      /* Low values (1-63) = Disable sync mode */
      disable_sync_mode(data);
    }
    else
    {
      /* Value 0 = Toggle sync mode */
      toggle_sync_mode(data);
    }

    pw_log_info("MIDI CC%d: Sync mode %s (value=%d)",
                controller, is_sync_mode_enabled(data) ? "ENABLED" : "DISABLED", value);
  }
  break;

  case SYNC_CUTOFF_CC_NUMBER:
  {
    /* Convert MIDI CC value (0-127) to sync playback cutoff percentage (0.0-1.0) */
    /* CC value 64 = 50% cutoff (default)
     * CC value 0 = 0% cutoff (always sync to current pulse)
     * CC value 127 = 100% cutoff (always wait for next pulse)
     */
    float cutoff_percentage = (float)(value & 0x7f) / 127.0f;

    data->sync_cutoff_percentage = cutoff_percentage;

    pw_log_info("MIDI CC%d: Sync playback cutoff set to %.1f%% (value=%d)",
                controller, cutoff_percentage * 100.0f, value);
  }
  break;

  case SYNC_RECORDING_CUTOFF_CC_NUMBER:
  {
    /* Convert MIDI CC value (0-127) to sync recording cutoff percentage (0.0-1.0) */
    /* CC value 64 = 50% cutoff (default)
     * CC value 0 = 0% cutoff (always start recording immediately with backfill)
     * CC value 127 = 100% cutoff (always wait for next pulse)
     */
    float recording_cutoff_percentage = (float)(value & 0x7f) / 127.0f;

    data->sync_recording_cutoff_percentage = recording_cutoff_percentage;

    pw_log_info("MIDI CC%d: Sync recording cutoff set to %.1f%% (value=%d)",
                controller, recording_cutoff_percentage * 100.0f, value);
  }
  break;

  default:
    pw_log_debug("Unhandled CC: controller=%d, value=%d", controller, value);
    break;
  }
}

void parse_midi_sequence(struct data *data, struct spa_pod_sequence *seq)
{
  struct spa_pod_control *c;

  SPA_POD_SEQUENCE_FOREACH(seq, c)
  {
    pw_log_trace("process_midi: found control at offset %u, type %d",
                 c->offset, c->type);

    if (c->type == SPA_CONTROL_UMP)
    {
      pw_log_trace("process_midi: found UMP control at offset %u", c->offset);

      if (SPA_POD_BODY_SIZE(&c->value) >= sizeof(uint32_t))
      {
        uint32_t *midi_data = (uint32_t *)SPA_POD_BODY(&c->value);
        if (midi_data != NULL)
        {
          pw_log_debug("MIDI input received: 0x%08x", *midi_data);
          data->reset_audio = true;
        }
      }
    }
    else if (c->type == SPA_CONTROL_Midi)
    {
      pw_log_trace("process_midi: found raw MIDI control at offset %u", c->offset);

      if (SPA_POD_BODY_SIZE(&c->value) >= sizeof(uint8_t))
      {
        uint8_t *midi_data = (uint8_t *)SPA_POD_BODY(&c->value);
        if (midi_data != NULL)
        {
          handle_midi_message(data, midi_data);
        }
      }
    }
  }
}

void process_midi_input(struct data *data, struct spa_io_position *position)
{
  struct pw_buffer *in_buf;

  if ((in_buf = pw_filter_dequeue_buffer(data->midi_in)) != NULL)
  {
    struct spa_data *in_d = &in_buf->buffer->datas[0];

    if (in_d->chunk->size > 0)
    {
      pw_log_trace("process_midi: received MIDI chunk of size %u",
                   in_d->chunk->size);

      // Parse the incoming MIDI data
      struct spa_pod *pod = spa_pod_from_data(in_d->data, in_d->chunk->size,
                                              in_d->chunk->offset, in_d->chunk->size);

      if (pod != NULL && spa_pod_is_sequence(pod))
      {
        struct spa_pod_sequence *seq = (struct spa_pod_sequence *)pod;
        parse_midi_sequence(data, seq);
      }
    }

    // Queue the input buffer back
    pw_filter_queue_buffer(data->midi_in, in_buf);
  }
}

void process_midi_output(struct data *data, struct spa_io_position *position)
{
  struct pw_buffer *buf;
  struct spa_data *d;
  struct spa_pod_builder builder;
  struct spa_pod_frame frame;
  uint64_t sample_offset, sample_period, sample_position, cycle;

  // Use the clock sample position
  if (data->clock_id != position->clock.id)
  {
    pw_log_info("switch to clock %u", position->clock.id);
    data->offset = position->clock.position - data->position;
    data->clock_id = position->clock.id;
  }

  sample_position = position->clock.position - data->offset;
  data->position = sample_position + position->clock.duration;

  // Produce note on/off every PERIOD_NSEC nanoseconds
  sample_period = PERIOD_NSEC * position->clock.rate.denom /
                  position->clock.rate.num / SPA_NSEC_PER_SEC;

  cycle = sample_position / sample_period;
  if (sample_position % sample_period != 0)
    ++cycle;

  sample_offset = cycle * sample_period - sample_position;

  if (sample_offset >= position->clock.duration)
    return; // don't need to produce anything yet

  // Get output buffer
  if ((buf = pw_filter_dequeue_buffer(data->midi_out)) == NULL)
    return;

  // MIDI buffers always have exactly one data block
  spa_assert(buf->buffer->n_datas == 1);

  d = &buf->buffer->datas[0];
  d->chunk->offset = 0;
  d->chunk->size = 0;
  d->chunk->stride = 1;
  d->chunk->flags = 0;

  // MIDI buffers contain a SPA POD with a sequence of control messages
  spa_pod_builder_init(&builder, d->data, d->maxsize);
  spa_pod_builder_push_sequence(&builder, &frame, 0);

  while (sample_offset < position->clock.duration)
  {
    if (cycle % 2 == 0)
    {
      // MIDI note on, channel 0, middle C, max velocity
      uint32_t event = 0x20903c7f;
      spa_pod_builder_control(&builder, sample_offset, SPA_CONTROL_UMP);
      spa_pod_builder_bytes(&builder, &event, sizeof(event));
      pw_log_info("note on at %" PRIu64, sample_position + sample_offset);
    }
    else
    {
      // MIDI note off, channel 0, middle C, max velocity
      uint32_t event = 0x20803c7f;
      spa_pod_builder_control(&builder, sample_offset, SPA_CONTROL_UMP);
      spa_pod_builder_bytes(&builder, &event, sizeof(event));
      pw_log_info("note off at %" PRIu64, sample_position + sample_offset);
    }

    sample_offset += sample_period;
    ++cycle;
  }

  // Finish the sequence and queue buffer to output
  spa_pod_builder_pop(&builder, &frame);
  d->chunk->size = builder.state.offset;

  pw_log_trace("produced %u/%u bytes", d->chunk->size, d->maxsize);
  pw_filter_queue_buffer(data->midi_out, buf);
}
