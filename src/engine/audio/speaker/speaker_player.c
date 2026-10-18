/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* effect records, patterns, noise and the speaker. */
#include "speaker_internal.h"

/* Flags a record keeps (the others are the record's own). */
enum { RECORD_KEEPS = 0xe4 };

void fe2_speaker_select(fe2_speaker *s, uint8_t effect)
{
    s->record = s->sounds->effects[effect];
    s->flags = (uint8_t)((s->flags | FE2_SPEAKER_NEXT_RECORD) & ~FE2_SPEAKER_STOPPED);
}

static void set_note(fe2_speaker *s, uint8_t note)
{
    s->game->set_pitch(s->game->opaque, s->sounds->divisors[note]);
}

static void disconnect(fe2_speaker *s)
{
    s->game->disconnect_speaker(s->game->opaque);
}

/* The effect's next record: the end stops the driver, a pause waits with
 * the speaker disconnected, and a note starts its pattern. */
void fe2_speaker_read_record(fe2_speaker *s)
{
    const uint8_t *record = s->record;
    uint8_t note, duration;
    s->flags &= RECORD_KEEPS;
    if (record[0] == FE2_SPEAKER_END) {
        s->flags |= FE2_SPEAKER_STOPPED | FE2_SPEAKER_DISCONNECTED;
        disconnect(s);
        return;
    }
    if (record[0] == FE2_SPEAKER_PAUSE) {
        s->flags |= FE2_SPEAKER_DISCONNECTED;
        disconnect(s);
        s->pause = record[1];
        s->record = record + 2;
        return;
    }
    s->command = s->sounds->patterns[record[0]];
    note = record[1];
    duration = record[2];
    if (note != FE2_SPEAKER_KEEP_NOTE) {
        s->note = note;
        set_note(s, note);
    }
    s->duration = duration;
    if (duration == 0) s->flags |= FE2_SPEAKER_HOLD;
    s->pause = 0;
    s->wait = 0;
    s->depth = 0;
    s->record = record + 3;
    if (s->flags & FE2_SPEAKER_DISCONNECTED) {
        s->flags &= (uint8_t)~FE2_SPEAKER_DISCONNECTED;
        s->game->connect_speaker(s->game->opaque);
    }
}

/* (254 * noise + 253) mod 65537, as 16 bits. */
uint16_t fe2_speaker_next_noise(uint16_t noise)
{
    return (uint16_t)((254u * noise + 253u) % 65537u);
}

/* A noisy note: a random step of up to 63 above the note, and the low
 * bits of the divisor raised by up to 255. */
static void noise(fe2_speaker *s)
{
    uint16_t value = s->noise = fe2_speaker_next_noise(s->noise);
    uint8_t note = (uint8_t)((value & 63u) + s->note);
    s->game->set_pitch(s->game->opaque, (uint16_t)(s->sounds->divisors[note] + (value >> 8)));
}

/* The note's duration counts down; at zero the next update reads the next
 * record. A held note waits for the pattern to finish it. */
static void count_duration(fe2_speaker *s)
{
    if (s->flags & FE2_SPEAKER_HOLD) return;
    if (s->duration == 0) s->flags |= FE2_SPEAKER_NEXT_RECORD;
    else --s->duration;
}

static void step_note(fe2_speaker *s)
{
    if (s->flags & FE2_SPEAKER_NOISE) noise(s);
    count_duration(s);
}

/* The pattern's commands up to its next note step, wait or finish. */
static void run_pattern(fe2_speaker *s)
{
    const uint8_t *command = s->command;
    for (;;) {
        uint8_t code = *command++;
        switch (code) {
        case FE2_SPEAKER_WAIT:
            s->wait = *command++;
            s->command = command;
            return;
        case FE2_SPEAKER_LOOP:
            if (s->depth < FE2_SPEAKER_LOOPS) {
                s->loops[s->depth].count = command[0];
                s->loops[s->depth].start = command + 1;
                ++s->depth;
            }
            ++command;
            continue;
        case FE2_SPEAKER_LOOP_END:
            /* Without a loop (a finished pattern) there is nothing to repeat. */
            if (s->depth == 0) continue;
            if (--s->loops[s->depth - 1].count != 0) command = s->loops[s->depth - 1].start;
            else --s->depth;
            continue;
        case FE2_SPEAKER_FINISH_NOTE:
            s->duration = 0;
            s->flags &= (uint8_t)~FE2_SPEAKER_HOLD;
            return;
        case FE2_SPEAKER_NOISE_ON:
            s->flags |= FE2_SPEAKER_NOISE;
            continue;
        case FE2_SPEAKER_NOISE_OFF:
            s->flags &= (uint8_t)~FE2_SPEAKER_NOISE;
            continue;
        default:
            s->note = (uint8_t)(s->note + code);
            s->command = command;
            if (!(s->flags & FE2_SPEAKER_NOISE)) set_note(s, s->note);
            return;
        }
    }
}

/* One update of the effect: its pause, or the pattern's wait, or the
 * pattern's next commands; then the note. (After the end record this
 * still steps the finished pattern once, with the speaker disconnected.) */
void fe2_speaker_advance(fe2_speaker *s)
{
    if (s->pause != 0) {
        if (--s->pause == 0) s->flags |= FE2_SPEAKER_NEXT_RECORD;
        return;
    }
    if (s->wait != 0) {
        --s->wait;
    } else {
        if (!s->command) return;
        run_pattern(s);
    }
    step_note(s);
}
