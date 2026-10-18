/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_PROGRAM_H
#define FE2_RENDER_PROGRAM_H

#include "expression.h"
#include "commands.h"

struct fe2_render_texture_output;

typedef enum fe2_render_program_status {
    FE2_RENDER_PROGRAM_COMPLETE,
    FE2_RENDER_PROGRAM_TRUNCATED,
    FE2_RENDER_PROGRAM_UNSUPPORTED,
    FE2_RENDER_PROGRAM_LIMIT,
    FE2_RENDER_PROGRAM_BAD_VALUE,
    FE2_RENDER_PROGRAM_OUTPUT_FULL
} fe2_render_program_status;

/* Geometry-command state kept per model invocation between commands
 * (original frame words BP-0x8e..BP-0xbb). Zero for a new invocation. */
typedef struct fe2_render_path_state {
    int first, last;              /* signed vertex references (BP-0xb0, BP-0xb4) */
    uint8_t first_set, last_set;
    uint16_t open, clipped;       /* BP-0xa6, BP-0xac */
    fe2_screen_point entry;       /* BP-0xaa/0xa8: near-plane entry point */
    int16_t products[3];          /* BP-0xba/0xb8/0xb6: last transformed normal */
    uint16_t caps[2];             /* BP-0x90/0x8e: opcode-17 cap selectors, reused by 16 */
} fe2_render_path_state;

typedef struct fe2_render_program_state {
    uint16_t local[64];
    fe2_render_words model, scale;
    fe2_render_path_state path;
} fe2_render_program_state;

typedef struct fe2_render_program_result {
    fe2_render_program_status status;
    size_t cursor; /* word index after success, or at offending command */
    size_t commands;
    size_t divide_resumes;
} fe2_render_program_result;

typedef struct fe2_render_program_drawing {
    /* Already-resolved model vertices. Negative signed-byte selectors may
     * address prefix_count records before vertices; positives use count. */
    const fe2_render_vertex *vertices;
    size_t prefix_count, vertex_count;
    int16_t scale;
    uint16_t face_base;
    uint32_t inherited_material;
    fe2_render_queue *queue;
    fe2_render_command *nodes;
    fe2_render_segment *segments;
    size_t capacity, count;
    /* Optional native math cache, preferred over the resolved vertex span.
     * Direct-reference bounds still use prefix_count/vertex_count. All
     * recursive program references must satisfy the math library's frame
     * contract (valid buffers/indices and terminating dependency graph). */
    const struct fe2_vertex_frame *vertex_frame;
    /* Optional solid-surface command storage and classification cache. */
    fe2_render_command *surface_nodes;
    fe2_render_surface_command *surfaces;
    size_t surface_capacity, surface_count;
    uint16_t *face_parameters;
    size_t face_parameter_count;
    const struct fe2_face_frame *face_frame;
    /* Nonzero selects the original detail mode 1 : triangles and
     * quads with table materials become textured commands (texture.h). */
    int textured_surfaces;
    struct fe2_render_texture_output *textures;
    struct fe2_render_execution *execution; /* optional native model hierarchy runtime */
} fe2_render_program_drawing;

/* Execute the native scalar/control subset of the model resource format:
 * 0 returns, 13/29 evaluate expressions, 19/20 filter by context mask. Other
 * commands report UNSUPPORTED; no geometry operation is silently skipped.
 * A false included-mask (or true excluded-mask) skips (token>>4)&0xfffe
 * bytes; a zero skip returns from the program. A branch may land exactly
 * at the end, but a subsequent fetch without a terminator is truncated.
 * Model data is a bounded word view, not an executable/CPU memory image.
 * Prior commands stay committed on error; the failing command changes no
 * local values and cursor identifies its start. Budget is in commands.
 * Program storage must not alias mutable state.local.
 */
fe2_render_program_result fe2_render_program_execute(fe2_render_words program,
                                                      fe2_render_program_state *state,
                                                      size_t budget);

/* Same operations directly on little-endian resource bytes. Cursor remains a
 * word index. An incomplete trailing byte cannot form a command/operand and
 * is truncated if execution reaches it; bytes after an earlier return are
 * ignored. The resource may be unaligned. */
fe2_render_program_result fe2_render_program_execute_encoded(const uint8_t *program, size_t bytes,
                                                              fe2_render_program_state *state,
                                                              size_t budget);

/* Add opcode 2 (material-selected, clipped lines), 3 (solid triangles),
 * 4 (solid quads), and 7/8 (adjusted then original triangles/quads) to the
 * scalar/control runner. Opcode 23 filters by projected Manhattan distance:
 * it consumes a threshold word and signed-byte vertex pair, requires each
 * depth >= 64, and sums absolute screen deltas modulo 65536. A clear threshold
 * sign skips when distance <= threshold; a set sign skips when distance >
 * its low 15 bits. Skip length is token>>5 words; zero skip returns. The
 * first shallow vertex suppresses access to the second reference. The filter
 * needs no queue or output buffers, but may refresh the native vertex cache.
 * Adjusted passes flip the low bit of each vertex
 * selector and the face selector. Each pair counts as one resource command.
 * Only accepted primitives consume output slots; exhaustion reports
 * OUTPUT_FULL at the offending command. The first pass of an adjusted pair
 * remains queued if its second pass fails; retrying needs caller cleanup.
 * Material words reach the backend unchanged;
 * palette mapping belongs to the backend. Geometry buffers/vertices must
 * outlive queue execution and must not alias state or resource storage.
 * When vertex_frame is supplied, native math generation/refresh is invoked
 * on stamp mismatches, low reference before high. Both records are read only
 * after those calls, since pair generation can replace the earlier vertex.
 * Native cache/noise state may change even when clipping rejects a line or
 * output is full. Otherwise callers supply already-resolved vertices.
 * Surface parameters equal to 0x8080 are classified through face_frame;
 * other values are cached answers. A negative answer culls without touching
 * vertices. Mixed/surface queues execute with fe2_render_drawing_target;
 * line-only queues also accept the original fe2_render_line_target.
 * Binding execution adds model instances (10), nested contexts (14/27),
 * command groups (6), detail filters (11/12), sound events (15), basis
 * controls (25/28), parameter accumulation (26), and context rebasing/basis
 * selection (30), including the 0x801e context-entry iterator. Inline round/
 * curve commands (1) and projected text commands (18) produce typed queued
 * payloads. These queues execute with fe2_render_execution_target. See
 * execution.h for workspace, service and lifetime contracts.
 * Descendant commands share this budget. On failure the cursor identifies
 * the parent resource command; counts include completed descendant commands.
 * Optimized triangle/quad rasterization and palette mapping are backend work. */
fe2_render_program_result fe2_render_program_execute_drawing(const uint8_t *program, size_t bytes,
                                                              fe2_render_program_state *state,
                                                              fe2_render_program_drawing *drawing,
                                                              size_t budget);

/* Predicate shared by the two context-mask commands. Source byte uses the
 * expression operand encoding. Mode byte zero or 129..255 tests the whole
 * value; other modes test 1<<((mode-1)&31) in a 16-bit word. */
int fe2_render_context_test(uint16_t operand, const fe2_render_expression_sources *sources, int *matches);

#endif
