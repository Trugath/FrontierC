/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_RENDER_EXECUTION_H
#define FE2_RENDER_EXECUTION_H

#include "model.h"
#include "program.h"

struct fe2_render_point_command;
struct fe2_render_point_output;

/* Transform and shading inputs for an explicit model invocation. Matrices use
 * the math library's nine-word layout. Model variables begin at object +0x72;
 * they are ordinary bounded word views, independent of descriptor storage. */
typedef struct fe2_render_model_transform {
    int16_t basis[9], selected_basis[9];
    uint16_t parity, selected_parity;
    int32_t position[3];
    int16_t direction[3];
    uint16_t shades[8];
    uint16_t exponent, scale;
    uint32_t material;
} fe2_render_model_transform;

typedef struct fe2_render_execution_frame {
    fe2_render_model_workspace workspace;
    fe2_render_model_context context;
    fe2_render_program_state state;
    fe2_render_model_transform transform;
    fe2_vertex_frame vertices;
    fe2_face_frame faces;
    fe2_vertex *inherited[5];
    fe2_render_bytes descriptor;
    size_t context_offset, vertex_offset;
} fe2_render_execution_frame;

typedef struct fe2_render_execution_services {
    void *context;
    /* Select an active/alternate bank descriptor. selector is the original
     * even model selector, not a machine address. Return zero on success.
     * Resource storage stays alive through the invocation. */
    int (*model)(void *context, uint16_t selector, int alternate, fe2_render_bytes *out);
    /* Text/template service used by model-instance opcode 10. Return a
     * bounded, zero-terminated character sequence, including its terminator.
     * Storage must survive recursive calls. Rendering directory traversal,
     * placement, culling and command execution are owned by this module. */
    int (*instance_text)(void *context, uint16_t selector, fe2_render_bytes *out);
    void (*sound)(void *context, uint16_t event, uint16_t duration);
} fe2_render_execution_services;

typedef struct fe2_render_command_group {
    fe2_render_command node;
    fe2_render_queue commands; /* equal keys retain resource emission order */
} fe2_render_command_group;

typedef enum fe2_render_round_kind {
    FE2_RENDER_DISC, FE2_RENDER_SMALL_DISC, FE2_RENDER_MARKER, FE2_RENDER_SHADED_CURVE
} fe2_render_round_kind;

typedef struct fe2_render_round_command {
    uint32_t key;
    fe2_render_round_kind kind;
    fe2_screen_point center;
    int16_t size;
    uint16_t material[2];
    fe2_screen_point curves[4][4]; /* two cubic contours, two cubics each */
} fe2_render_round_command;

typedef struct fe2_render_text_command {
    uint32_t key;
    uint16_t selector, parameter[3];
    fe2_screen_point position;
} fe2_render_text_command;

struct fe2_render_path_command;
struct fe2_render_curve_command;
struct fe2_render_path_output;
struct fe2_render_sphere_command;
struct fe2_render_sphere_output;
typedef struct fe2_render_execution_target {
    fe2_render_drawing_target drawing; /* existing line/surface targets first */
    void (*round)(void *context, const fe2_render_round_command *command);
    void (*text)(void *context, const fe2_render_text_command *command);
    void *context;
    /* Opcode 5/9/16/17 filled paths and opcode 22 cubic strokes (path.h). */
    void (*path)(void *context, const struct fe2_render_path_command *command);
    void (*curve)(void *context, const struct fe2_render_curve_command *command);
    /* Opcode 24 solid-disc point lists (points.h). */
    void (*points)(void *context, const struct fe2_render_point_command *command);
    /* Opcode 31 spheres (sphere.h). */
    void (*sphere)(void *context, const struct fe2_render_sphere_command *command);
} fe2_render_execution_target;

typedef struct fe2_render_execution {
    fe2_render_execution_services services;
    fe2_render_execution_frame *frames;
    size_t frame_capacity, depth;
    fe2_vertex_work *vertex_work;
    uint16_t detail_shift[2]; /* solid/textured detail threshold exponents */
    fe2_render_command_group *groups, *active_group;
    size_t group_capacity, group_count;
    fe2_render_command *round_nodes, *text_nodes;
    fe2_render_round_command *rounds;
    fe2_render_text_command *texts;
    size_t round_capacity, round_count, text_capacity, text_count;
    /* Persistent attachment selections, corresponding to prefix -5..-2.
     * Caller may seed these. Model commands update them in recovered order.
     * Parent records must remain alive while their descendants execute. */
    fe2_vertex *attachments[4];
    /* Opcode 21 depth-sorted batches (curve record batches). trees[0] is the
     * active insertion queue, trees[1..3] the saved ones; NULL is the root
     * drawing queue. Zero these with batch_count when starting a new frame. */
    fe2_render_command_group *batches;
    size_t batch_capacity, batch_count;
    fe2_render_queue *trees[4];
    /* Path/curve storage for geometry opcodes 5, 9, 16, 17 and 22 (path.h). */
    struct fe2_render_path_output *paths;
    /* Point-list storage for opcode 24 (points.h). */
    struct fe2_render_point_output *points;
    /* Sphere storage for opcode 31 (sphere.h); its ring layers use paths. */
    struct fe2_render_sphere_output *spheres;
} fe2_render_execution;

/* Explicit-frame entry, including descriptor visibility, native workspace
 * initialization, vertex/face frame binding and recursive command execution.
 * frames[depth] supplies disjoint workspace for this call. Zero-initialize
 * slots before first use. A 0x4000 child mode reuses its slot's previous view
 * and direction; other slot fields are outputs. Model variables and scale
 * tables are inherited by descendants. Normal child locals inherit depth+1
 * and parent words 1/2; remaining locals and instance locals start at zero.
 * External context optionally replaces the descriptor's vertex program and
 * provides its declared vertex count. No buffer is allocated internally.
 * Output counts and cache/noise effects survive errors; runtime depth and all
 * caller drawing bindings are restored. Command budget includes descendants.
 * A culled invocation returns COMPLETE with no commands. Root exit closes any
 * active group; groups and command payloads remain alive through queue drawing.
 * group_count, round_count, text_count and drawing output counts are caller-
 * owned allocation cursors; reset them and the root queue together for a new frame. Instance slots need
 * at least 132 parameter words. Descriptor/text services must not mutate this
 * runtime or drawing state; their storage must survive recursive calls.
 * Vertex dependency graphs and face normals satisfy the math frame contract.
 * 0x801e executes context entries, advancing bases and producing optional lines,
 * round geometry and depth-conditioned text labels. All table/model accesses
 * are bounded; completed emissions and cache/base changes survive failure.
 * Queues containing round/text payloads use fe2_render_execution_target;
 * pixel drawing and text formatting are supplied through backend callbacks.
 * Missing required services/output storage produce explicit errors. */
fe2_render_program_result fe2_render_execute_model(fe2_render_execution *execution,
    fe2_render_bytes descriptor, const fe2_render_model_transform *transform,
    fe2_render_words variables, fe2_render_words scales,
    const fe2_render_bytes *external_context, size_t external_vertex_count,
    fe2_render_program_drawing *drawing, size_t budget);

/* Native model-program services. Called by the bounded resource runner;
 * operands exclude the command word. cursor is the number of operand words
 * consumed on success. commands counts executed descendant commands only. */
fe2_render_program_result fe2_render_execute_model_command(uint16_t token,
    fe2_render_words operands, fe2_render_program_state *state,
    fe2_render_program_drawing *drawing, size_t budget);

fe2_render_program_status fe2_render_model_threshold(uint16_t token, uint16_t threshold,
    fe2_render_program_drawing *drawing, int *skip);

fe2_render_program_result fe2_render_execute_group_command(uint16_t token,
    fe2_render_bytes operands, fe2_render_program_drawing *drawing);
int fe2_render_execution_insert(fe2_render_program_drawing *drawing, fe2_render_command *node,
    uint32_t key, fe2_render_command_fn execute, const void *payload);
/* Active sorted insertion queue: the innermost opcode-21 batch, else the root. */
fe2_render_queue *fe2_render_execution_tree(fe2_render_program_drawing *drawing);
/* equivalent: place (generate/refresh) a signed vertex reference. */
fe2_vertex *fe2_render_execution_place(fe2_render_program_drawing *drawing, int index);
/* equivalent: refresh a signed vertex reference on a stamp mismatch. */
fe2_vertex *fe2_render_execution_refresh(fe2_render_program_drawing *drawing, int index);
/* Opcode 21: positive tokens open a batch keyed by vertex (token>>5 low byte,
 * signed) inside the active queue; set bit 15 closes the innermost one. */
fe2_render_program_status fe2_render_execute_batch_command(uint16_t token,
    fe2_render_program_drawing *drawing);

fe2_render_program_status fe2_render_submit_model_line(fe2_render_program_drawing *drawing, uint16_t token, uint16_t pair);
fe2_render_program_status fe2_render_submit_round(uint16_t token, uint16_t size, uint16_t control,
    fe2_render_program_state *state, fe2_render_program_drawing *drawing);
fe2_render_program_status fe2_render_submit_text(uint16_t token, uint16_t selector,
    fe2_render_program_state *state, fe2_render_program_drawing *drawing);
/* Round/curve producer over a resolved camera record and projected cache.
 * radius is the already-scaled model diameter. Material words are unmapped.
 * Return 1 emitted, 0 culled, -1 invalid (including an unavailable coefficient
 * sample). Output stays unchanged on 0/-1. No machine frame is needed. */
int fe2_render_build_round(const fe2_render_vertex *vertex, uint32_t radius, uint16_t token,
    uint16_t control, const fe2_render_model_transform *transform, fe2_render_round_command *out);
extern const uint8_t *fe2_render_context_entry_data;
int fe2_render_build_distant(const fe2_render_vertex *vertex, uint16_t radius, uint16_t token,
    const fe2_render_model_transform *transform, fe2_render_round_command *out);
extern uint32_t fe2_render_context_entry_bytes;
extern const uint8_t *fe2_render_curve_coefficients;

/* Six column permutations or selector 6 (preselected basis), with wrapped
 * negation and orientation parity. Selector 7 is outside the valid domain.
 * View preparation/retention is handled by the invocation, not this helper. */
int fe2_render_inherit_model_transform(const fe2_render_model_transform *parent,
    uint16_t mode, const int32_t position[3], fe2_render_model_transform *out);

#endif
