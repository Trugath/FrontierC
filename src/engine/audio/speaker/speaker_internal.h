/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SPEAKER_INTERNAL_H
#define FE2_SPEAKER_INTERNAL_H

#include "speaker.h"

void fe2_speaker_select(fe2_speaker *s, uint8_t effect);
void fe2_speaker_read_record(fe2_speaker *s);
void fe2_speaker_advance(fe2_speaker *s);

#endif
