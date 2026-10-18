/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galactic_map_internal.h"
#include "galactic_map_tables.h"

int map_density_pass(fe2_map_state *state,const fe2_map_services *game,unsigned step,int *interrupted)
{
    uint16_t count=state->sample_count,spacing=(uint16_t)(count*step);
    uint16_t start=(uint16_t)((state->center_y>>16)-count*step/2u+160u*count);
    uint16_t vertical=(uint16_t)((state->center_x>>16)+count*step/2u-84u*count);
    int row; unsigned column;
    if(!interrupted || !count || count>254 || (step!=1 && step!=2 && step!=4)) return FE2_MAP_INVALID;
    *interrupted=0;
    /* Native fine pass subtracts count/2, not a rounded half of the products. */
    if(step==1) {
        start=(uint16_t)((state->center_y>>16)-count/2u+160u*count);
        vertical=(uint16_t)((state->center_x>>16)+count/2u-84u*count);
    }
    for(row=168-(int)step;row>=0;row-=(int)step) {
        uint16_t horizontal=start;
        for(column=320-step;;column-=step) {
            uint16_t density; uint8_t colour;
            MAP_TRY(fe2_map_density(game,horizontal,vertical,(uint16_t)(count-1),&density));
            colour=map_density_colours[(density>>9)&126u];
            if(step==1) MAP_TRY(MAP_CALL(game,draw_pixel,(uint16_t)column,(uint8_t)row,colour));
            else MAP_TRY(MAP_CALL(game,draw_block,(uint16_t)column,(uint16_t)(column+step-1),(uint8_t)row,(uint8_t)(row+step-1),colour));
            horizontal=(uint16_t)(horizontal-spacing);
            if(column<step) break;
        }
        if(step<4 && game && game->input_pending && game->input_pending(game->opaque)) { *interrupted=1; return 0; }
        vertical=(uint16_t)(vertical+spacing);
    }
    return 0;
}

int map_density_annotations(fe2_map_state *state,const fe2_map_services *game)
{
    unsigned i; fe2_call_args args={0};
    if(!state->sample_count || state->sample_count>254) return FE2_MAP_INVALID;
    for(i=0;i<14;++i) {
        const map_annotation *note=&map_annotations[i]; int py,px;
        if(map_s16(note->threshold)<state->sample_count) break;
        px=map_s16((uint16_t)(note->y-(state->center_y>>16)))/(int)state->sample_count+162;
        py=81-map_s16((uint16_t)(note->x-(state->center_x>>16)))/(int)state->sample_count;
        if(map_s16((uint16_t)py)<=159) MAP_TRY(map_text(game,note->text,&args,map_annotation_colours[0],(uint16_t)px,(uint16_t)py));
    }
    MAP_TRY(MAP_CALL(game,draw_crosshair,160,84,map_annotation_colours[1]));
    fe2_args_dword(&args,1,500u*state->sample_count);
    MAP_TRY(map_text(game,0x13e,&args,map_annotation_colours[2],19,141));
    MAP_TRY(MAP_CALL(game,draw_line,20,153,82,153,map_annotation_colours[2]));
    MAP_TRY(MAP_CALL(game,draw_line,20,151,20,156,map_annotation_colours[2]));
    return MAP_CALL(game,draw_line,82,151,82,156,map_annotation_colours[2]);
}

static int refresh(fe2_map_state *state,const fe2_map_services *game)
{
    int interrupted;
    state->refresh_phase=2; state->seed.a=0x12345678; state->seed.b=0x87654321;
    MAP_TRY(map_density_pass(state,game,4,&interrupted)); MAP_TRY(map_density_annotations(state,game));
    return MAP_SIMPLE(game,present);
}

int fe2_map_enter_density(fe2_map_state *state,const fe2_map_services *game)
{
    if(!state || !state->sample_count || state->sample_count>254) return FE2_MAP_INVALID;
    MAP_TRY(MAP_CALL(game,set_palette,map_density_palette,128)); state->mode=2;
    MAP_TRY(map_button(game,2,256,188)); return refresh(state,game);
}

int fe2_map_update_density(fe2_map_state *state,const fe2_map_services *game,const fe2_map_input *input)
{
    uint8_t key; int interrupted;
    if(!state || !input || !state->sample_count || state->sample_count>254) return FE2_MAP_INVALID;
    key=input->has_command ? input->command : 0;
    if(key) {
        if(key==0xf7) { if(state->sample_count==1) return 0; --state->sample_count; return refresh(state,game); }
        if(key==0xf8) { if(state->sample_count>=254) return 0; ++state->sample_count; return refresh(state,game); }
        if(key==0xf9) return 0;
        if(key!=0xfa) MAP_TRY(MAP_CALL(game,forward_command,key));
        state->mode=-1; MAP_TRY(MAP_SIMPLE(game,reset_view)); MAP_TRY(map_reset_menu(state,game));
        { fe2_map_input next=*input; next.has_command=0; return fe2_map_update(state,game,&next); }
    }
    if(!state->refresh_phase) return 0;
    --state->refresh_phase;
    MAP_TRY(map_density_pass(state,game,state->refresh_phase ? 2 : 1,&interrupted));
    if(interrupted) { state->refresh_phase=2; return 0; }
    MAP_TRY(map_density_annotations(state,game)); return MAP_SIMPLE(game,present);
}
