#include "uphonor.h"

#define PERIOD_NSEC (SPA_NSEC_PER_SEC / 8)

void process_midi(void *userdata, struct spa_io_position *position)
{
  struct data *data = userdata;
  struct port *midi_output = data->midi_out;
  struct port *midi_input = data->midi_in;
  struct pw_buffer *buf;
  struct spa_data *d;
  struct spa_pod_builder builder;
  struct spa_pod_frame frame;
  uint64_t sample_offset, sample_period, sample_position, cycle;
  // Process incoming MIDI data first
  struct pw_buffer *in_buf;
  if ((in_buf = pw_filter_dequeue_buffer(midi_input)) != NULL)
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
        struct spa_pod_control *c;

        SPA_POD_SEQUENCE_FOREACH(seq, c)
        {
          pw_log_trace("process_midi: found control at offset %u, type %d",
                       c->offset, c->type);
          if (c->type == SPA_CONTROL_UMP)
          {
            pw_log_trace("process_midi: found UMP control at offset %u",
                         c->offset);
            // Access the MIDI data directly from the control value
            if (SPA_POD_BODY_SIZE(&c->value) >= sizeof(uint32_t))
            {
              uint32_t *midi_data = (uint32_t *)SPA_POD_BODY(&c->value);
              if (midi_data != NULL)
              {
                pw_log_debug("MIDI input received: 0x%08x", *midi_data);
                // Any MIDI input triggers audio reset
                data->reset_audio = true;
                pw_log_debug("MIDI input received (0x%08x)", *midi_data);
                break;
              }
            }
          }
          else if (c->type == SPA_CONTROL_Midi)
          {
            pw_log_trace("process_midi: found raw MIDI control at offset %u",
                         c->offset);
            // Handle raw MIDI data if needed
            if (SPA_POD_BODY_SIZE(&c->value) >= sizeof(uint8_t))
            {
              uint8_t *midi_data = (uint8_t *)SPA_POD_BODY(&c->value);
              if (midi_data == NULL)
              {
                pw_log_trace("process_midi: unknown control type %d at offset %u",
                             c->type, c->offset);
                continue;
              }
              pw_log_trace("Raw MIDI input received: 0x%02x", *midi_data);

              // Check for Note On messages
              if ((*midi_data & 0xf0) == 0x80)
              {
                pw_log_debug("Note Off message received: 0x%02x", *midi_data);
              }
              if ((*midi_data & 0xf0) == 0x90)
              {
                pw_log_debug("Note On message received: 0x%02x", *midi_data);
                pw_log_info("Resetting audio playback due to Note On message");
                data->reset_audio = true;
                // set the volume from the Note On message velocity
                uint8_t velocity = *(midi_data + 2);
                float volume = (float)(velocity & 0x7f) / 127.0f; // Normalize velocity to 0.0-1.0
                pw_log_info("Setting volume to %.2f from Note On velocity %d",
                            volume, velocity);
                data->volume = volume;
              }
              else if ((*midi_data & 0xf0) == 0xA0)
              {
                pw_log_debug("Polyphonic Aftertouch message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xB0)
              {
                pw_log_debug("Control Change message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xC0)
              {
                pw_log_debug("Program Change message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xD0)
              {
                pw_log_debug("Channel Pressure message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xE0)
              {
                pw_log_debug("Pitch Bend message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xF8)
              {
                pw_log_debug("Timing Clock message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xFA)
              {
                pw_log_debug("Start message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xFB)
              {
                pw_log_debug("Continue message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xFC)
              {
                pw_log_debug("Stop message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xFE)
              {
                pw_log_debug("Active Sensing message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xFF)
              {
                pw_log_debug("System Reset message received: 0x%02x", *midi_data);
              }
              else if ((*midi_data & 0xf0) == 0xf0)
              {
                pw_log_debug("System Exclusive message received: 0x%02x", *midi_data);
              }
              else
              {
                pw_log_trace("Unknown MIDI message type: 0x%02x", *midi_data);
              }
            }
          }
        }
      }
    }
    // Queue the input buffer back
    pw_filter_queue_buffer(midi_input, in_buf);
  }
  /*
   * Use the clock sample position.
   *
   * If the playback switches to using a different clock, we reset
   * playback as the sample position can then be discontinuous.
   */
  pw_log_trace("on_process: clock id %u, position %" PRIu64 ", duration %" PRIu64,
               position->clock.id, position->clock.position, position->clock.duration);
  if (data->clock_id != position->clock.id)
  {
    pw_log_info("switch to clock %u", position->clock.id);
    data->offset = position->clock.position - data->position;
    data->clock_id = position->clock.id;
  }

  sample_position = position->clock.position - data->offset;
  data->position = sample_position + position->clock.duration;

  /*
   * Produce note on/off every `PERIOD_NSEC` nanoseconds (rounded down to
   * samples, for simplicity).
   *
   * We want to place the notes on the playback timeline, so we use sample
   * positions (not real time!).
   */

  sample_period = PERIOD_NSEC * position->clock.rate.denom / position->clock.rate.num / SPA_NSEC_PER_SEC;

  cycle = sample_position / sample_period;
  if (sample_position % sample_period != 0)
    ++cycle;

  sample_offset = cycle * sample_period - sample_position;

  if (sample_offset >= position->clock.duration)
    return; /* don't need to produce anything yet */

  /* Get output buffer */
  if ((buf = pw_filter_dequeue_buffer(midi_output)) == NULL)
    return;

  /* Midi buffers always have exactly one data block */
  spa_assert(buf->buffer->n_datas == 1);

  d = &buf->buffer->datas[0];
  d->chunk->offset = 0;
  d->chunk->size = 0;
  d->chunk->stride = 1;
  d->chunk->flags = 0;

  /*
   * MIDI buffers contain a SPA POD with a sequence of
   * control messages and their raw MIDI data.
   */
  spa_pod_builder_init(&builder, d->data, d->maxsize);
  spa_pod_builder_push_sequence(&builder, &frame, 0);

  while (sample_offset < position->clock.duration)
  {
    if (cycle % 2 == 0)
    {
      /* MIDI note on, channel 0, middle C, max velocity */
      uint32_t event = 0x20903c7f;

      /* The time position of the message in the graph cycle
       * is given as offset from the cycle start, in
       * samples. The cycle has duration of `clock.duration`
       * samples, and the sample offset should satisfy
       * 0 <= sample_offset < position->clock.duration.
       */
      spa_pod_builder_control(&builder, sample_offset, SPA_CONTROL_UMP);

      /* Raw MIDI data for the message */
      spa_pod_builder_bytes(&builder, &event, sizeof(event));

      pw_log_info("note on at %" PRIu64, sample_position + sample_offset);
    }
    else
    {
      /* MIDI note off, channel 0, middle C, max velocity */
      uint32_t event = 0x20803c7f;

      spa_pod_builder_control(&builder, sample_offset, SPA_CONTROL_UMP);
      spa_pod_builder_bytes(&builder, &event, sizeof(event));

      pw_log_info("note off at %" PRIu64, sample_position + sample_offset);
    }

    sample_offset += sample_period;
    ++cycle;
  }

  /*
   * Finish the sequence and queue buffer to output.
   */
  spa_pod_builder_pop(&builder, &frame);
  d->chunk->size = builder.state.offset;

  pw_log_trace("produced %u/%u bytes", d->chunk->size, d->maxsize);

  pw_filter_queue_buffer(midi_output, buf);
}

static void state_changed(void *userdata, enum pw_filter_state old,
                          enum pw_filter_state state, const char *error)
{
  struct data *data = userdata;

  switch (state)
  {
  case PW_FILTER_STATE_STREAMING:
    /* reset playback position */
    pw_log_info("start playback");
    data->clock_id = SPA_ID_INVALID;
    data->offset = 0;
    data->position = 0;
    break;
  default:
    break;
  }
}

static const struct pw_filter_events filter_events = {
    PW_VERSION_FILTER_EVENTS,
    .process = process_midi,
    .state_changed = state_changed,
};
