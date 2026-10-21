/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galactic_map_internal.h"
#include "galactic_map_tables.h"
#include <string.h>

int map_button(const fe2_map_services *game,unsigned button,unsigned x,unsigned y)
{ return MAP_CALL(game,draw_button,(uint16_t)button,(uint16_t)x,(uint16_t)y); }

int map_reset_menu(fe2_map_state *state,const fe2_map_services *game)
{ state->menu_open=0; return map_button(game,1,256,188); }

int fe2_map_select(fe2_map_state *state,const fe2_map_services *game,uint16_t index,uint16_t descriptor)
{
    fe2_map_details details={0};
    uint16_t y,x;
    uint16_t low; size_t prefix=0;
    fe2_call_args args={0};
    if(!state) return FE2_MAP_INVALID;
    y=(uint16_t)(state->selected.y>>16); x=(uint16_t)(state->selected.x>>16);
    state->selected_index=index; state->selected_descriptor=descriptor;
    MAP_TRY(fe2_map_name(game,y,x,index,state->selected_name,sizeof state->selected_name));
    state->selected_classification[0]=0;
    if(descriptor&7u) {
        MAP_TRY(fe2_map_format_text(game,(uint16_t)(0x130u+2*(descriptor&7u)),y,x,index,0,&args,
            state->selected_classification,sizeof state->selected_classification));
        prefix=strlen(state->selected_classification);
    }
    MAP_TRY(fe2_map_format_text(game,(uint16_t)(0x110u+((descriptor>>8)&126u)),y,x,index,0,&args,
        state->selected_classification+prefix,sizeof state->selected_classification-prefix));
    MAP_TRY(fe2_map_distance(game,&state->selected,&state->home,&state->selection_distance));
    state->selection_mix=state->selected.y+((state->selected.x<<16)|(state->selected.x>>16))-state->selected.z;
    /* The index addition is a low-word addition without carry into X. */
    low=(uint16_t)(((uint16_t)y<<6)+index);
    state->selection_reference=((((uint32_t)x<<19)|((uint32_t)(y&8191u)<<6))&UINT32_C(0xffff0000))|low;
    MAP_TRY(MAP_CALL(game,system_details,state->selection_reference,0,&details));
    state->detail_status=details.status; state->detail_result_a=details.result_a; state->detail_result_b=details.result_b;
    if(descriptor&0x8000u) state->selection_mix=0;
    state->selection_value=map_interpolate(descriptor,0);
    state->selection_distance = fe2_map_jump_distance(state->selection_distance,
        state->selection_reference, state->current_system);
    if(!state->selection_distance) state->travel_result=0;
    else {
        uint16_t travel; int negative;
        MAP_TRY(MAP_CALL(game,travel,state->ship,state->selection_distance,&travel,&negative));
        state->travel_result=travel&32768u ? 65535u : (uint16_t)(travel|(negative ? 32768u : 0u));
    }
    return 0;
}

int fe2_map_initialize(fe2_map_state *state,const fe2_map_services *game,unsigned start)
{
    if(!state || start>=6) return FE2_MAP_INVALID;
    state->home=map_starts[start].position; state->selected=state->home;
    state->center_y=state->home.y; state->center_x=state->home.x; state->depth=100000;
    MAP_TRY(fe2_map_select(state,game,map_starts[start].index,map_swap16(map_starts[start].descriptor)));
    state->display_flags=0x61; state->sample_count=1; state->menu_open=0;
    return 0;
}

int fe2_map_activate(fe2_map_state *state,const fe2_map_services *game)
{
    if(!state) return FE2_MAP_INVALID;
    if(state->mode!=0) return MAP_CALL(game,secondary_view,1);
    MAP_TRY(map_button(game,11,16,188));
    MAP_TRY(MAP_CALL(game,primary_panel,4,0x84b2));
    MAP_TRY(MAP_CALL(game,set_panel_mode,2));
    state->selection_status=0; state->selection_dirty=255;
    MAP_TRY(MAP_SIMPLE(game,clear_projection)); MAP_TRY(MAP_CALL(game,set_controls,0x303,0x13));
    state->mode=-1;
    return map_button(game,1,256,188);
}

int fe2_map_enter_view(fe2_map_state *state,const fe2_map_services *game,int16_t request,int *changed)
{
    if(!state || !changed) return FE2_MAP_INVALID;
    if(!request) {
        MAP_TRY(MAP_SIMPLE(game,reset_view)); state->mode=0;
        MAP_TRY(map_button(game,10,16,188)); MAP_TRY(MAP_CALL(game,primary_panel,0,0));
        *changed=1; return 0;
    }
    if(request<0) { *changed=1; return 0; }
    *changed=state->mode!=0;
    if(*changed) {
        state->selection_status=0; state->selection_dirty=255;
        MAP_TRY(map_button(game,state->mode<0 ? 9 : 8,240,186));
    }
    return 0;
}

void fe2_map_promote_selection(fe2_map_state *state)
{
    state->home=state->selected; state->selection_dirty=255;
    state->travel_result=0; state->selection_distance=0;
}

int fe2_map_perturb_home(fe2_map_state *state,const fe2_map_services *game,uint16_t *range)
{
    uint32_t sample,reference; fe2_map_query result;
    unsigned i; int wide; fe2_map_position home;
    if(!state || !range) return FE2_MAP_INVALID;
    wide=*range>=4;
    home=state->home;
    for(i=0;i<3;++i) {
        MAP_TRY(MAP_CALL(game,random,&sample));
        if(i==2) home.z+=(uint32_t)map_s16(sample);
        else if(wide) {
            if(i) home.x+=map_sar(sample,8); else home.y+=map_sar(sample,8);
        } else if(i) home.x=(home.x&0xffff0000u)|(uint16_t)(home.x+sample);
        else home.y=(home.y&0xffff0000u)|(uint16_t)(home.y+sample);
    }
    state->home=home;
    MAP_TRY(fe2_map_query_system(state,game,state->current_system,2,&result));
    state->selection_distance=result.available ? result.distance : UINT32_MAX;
    if(wide) return 0;
    reference=state->current_system&UINT32_C(0xffffffc0);
    for(;;) {
        MAP_TRY(fe2_map_query_system(state,game,reference,2,&result));
        if(!result.available || result.distance&UINT32_C(0x80000000)) return 0;
        if(result.distance<=80) break;
        reference=(reference&0xffff0000u)|(uint16_t)(reference+1u);
    }
    state->selected=result.position;
    MAP_TRY(fe2_map_select(state,game,result.index,map_swap16(result.descriptor)));
    *range=(uint16_t)(*range-2u);
    return 0;
}

static int return_to_map(fe2_map_state *state,const fe2_map_services *game)
{
    state->mode=-1; MAP_TRY(MAP_SIMPLE(game,reset_view));
    MAP_TRY(map_button(game,8,240,186)); return map_reset_menu(state,game);
}

int fe2_map_update(fe2_map_state *state,const fe2_map_services *game,const fe2_map_input *input)
{
    uint32_t pan,depth; uint8_t key;
    if(!state || !input) return FE2_MAP_INVALID;
    key=input->command;
    if(state->mode==2) return fe2_map_update_density(state,game,input);
    if(state->mode>=0) {
        if(!input->has_command || !key) return 0;
        if(key==0xf6) {
            fe2_map_input next=*input; next.has_command=0;
            MAP_TRY(return_to_map(state,game)); return fe2_map_update(state,game,&next);
        }
        if(key==0xf7) return fe2_map_draw_trade(state,game);
        if(key==0xf8) return fe2_map_draw_government(state,game);
        if(key==0xfa) return state->detail_status ? MAP_CALL(game,secondary_view,0) : 0;
        if(key>=0x80 && key<=0xbc) return fe2_map_draw_body(state,game,key&127u);
        if(key==0xff) return map_system_text(state,game);
        return MAP_CALL(game,forward_command,key);
    }
    pan=input->fast ? 0xe000u : 0x800u; depth=input->fast ? 8000u : 4000u;
    if(input->pan&FE2_MAP_PAN_Y_MINUS) state->center_y-=pan;
    if(input->pan&FE2_MAP_PAN_Y_PLUS) state->center_y+=pan;
    if(input->pan&FE2_MAP_PAN_X_PLUS) state->center_x+=pan;
    if(input->pan&FE2_MAP_PAN_X_MINUS) state->center_x-=pan;
    if(input->pan) state->selection_dirty=1;
    if(input->has_command) {
        if(state->menu_open) {
            if(key>=0xf7 && key<=0xfa) state->display_flags^=(uint16_t[]){0x20,1,8,0x40}[key-0xf7];
            MAP_TRY(map_reset_menu(state,game));
        } else if(key==0xfa) return fe2_map_enter_density(state,game);
        else if(key==0x63) {
            state->selected=state->home; state->center_y=state->home.y; state->center_x=state->home.x;
            MAP_TRY(MAP_SIMPLE(game,clear_projection)); state->depth=140000; state->selection_dirty=255;
        } else if(state->selection_status && key==0xf6) return fe2_map_draw_system(state,game);
        else if(key==0xf7) state->depth-=depth;
        else if(key==0xf8) state->depth+=depth;
        else if(key==0xf9) {
            MAP_TRY(map_button(game,3,256,188));
            if(!(state->display_flags&0x20)) MAP_TRY(map_button(game,4,256,188));
            if(!(state->display_flags&1)) MAP_TRY(map_button(game,5,272,188));
            if(!(state->display_flags&8)) MAP_TRY(map_button(game,6,288,188));
            if(!(state->display_flags&0x40)) MAP_TRY(map_button(game,7,304,188));
            state->menu_open=1;
        } else MAP_TRY(MAP_CALL(game,forward_command,key));
    }
    return fe2_map_draw(state,game);
}
