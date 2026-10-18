/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galactic_map_internal.h"
#include <string.h>

void fe2_map_project_vector(const int16_t matrix[9],const int16_t vector[3],uint32_t projected[3])
{
    unsigned column,row;
    for(column=0;column<3;++column) {
        uint32_t sum=0;
        for(row=0;row<3;++row) sum+=(uint32_t)((int32_t)matrix[row*3+column]*vector[row]);
        projected[column]=sum<<1;
    }
}

int map_project(const fe2_map_services *game,const int16_t matrix[9],const int16_t vector[3],uint32_t projected[3])
{
    if(game && game->project) return game->project(game->opaque,matrix,vector,projected);
    fe2_map_project_vector(matrix,vector,projected); return 0;
}

static void vector_add(uint32_t out[3],const uint32_t base[3],const uint32_t y[3],const uint32_t x[3],int dy,int dx)
{
    unsigned i;
    for(i=0;i<3;++i) out[i]=base[i]+(uint32_t)dy*y[i]+(uint32_t)dx*x[i];
}

static int marker(fe2_map_state *state,const fe2_map_services *game,const int16_t matrix[9],
                   const fe2_map_position *position,unsigned kind,uint32_t range)
{
    uint32_t relative[3]={position->y-state->center_y,position->x-state->center_x,position->z},projected[3];
    int16_t input[3]; fe2_map_scene scene={0}; unsigned i;
    int high_y=map_s16(relative[0]>>16),high_x=map_s16(relative[1]>>16);
    if(high_y < -2 || high_y > 1 || high_x < -2 || high_x > 1) return 0;
    input[0]=(int16_t)map_s16(map_sar(relative[2],2));
    input[1]=(int16_t)map_s16(map_sar(relative[1],2));
    input[2]=(int16_t)map_s16(map_sar(relative[0],2));
    MAP_TRY(map_project(game,matrix,input,projected));
    for(i=0;i<3;++i) scene.position[i]=map_sar(projected[i],14);
    scene.position[2]+=state->depth; scene.flags=state->display_flags;
    memcpy(scene.basis,matrix,sizeof scene.basis);
    scene.draw_kind=(uint16_t)kind; scene.marker_range=(uint16_t)range;
    return MAP_CALL(game,draw_scene,&scene,1);
}

int fe2_map_draw(fe2_map_state *state,const fe2_map_services *game)
{
    static const int neighbours[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    int16_t matrix[9],original[9],input[3]; uint16_t angles[2];
    uint32_t center[3],axis_y[3],axis_x[3],range;
    fe2_map_projection projected;
    fe2_map_scene scene={0}; unsigned i,j; int nearest;
    uint16_t y,x;
    if(!state) return FE2_MAP_INVALID;
    y=(uint16_t)(state->center_y>>16); x=(uint16_t)(state->center_x>>16);
    projected.flags=state->display_flags;
    MAP_TRY(MAP_CALL(game,basis,angles,original));
    memcpy(matrix,original,sizeof matrix);
    matrix[0]=original[7]; matrix[4]=original[0]; matrix[2]=original[4]; matrix[7]=original[2];
    matrix[3]=original[6]; matrix[5]=original[3]; matrix[8]=original[5]; matrix[6]=original[8];
    memcpy(scene.basis,matrix,sizeof scene.basis);
    input[0]=0;
    /* Negation follows arithmetic shift, including negative odd input words. */
    input[1]=(int16_t)-map_s32(map_sar((uint32_t)(int32_t)map_s16((uint16_t)(state->center_x+0x8000u)),1));
    input[2]=(int16_t)-map_s32(map_sar((uint32_t)(int32_t)map_s16((uint16_t)(state->center_y+0x8000u)),1));
    MAP_TRY(map_project(game,matrix,input,center));
    for(i=0;i<3;++i) center[i]=map_sar(center[i],15);
    input[0]=0; input[1]=0; input[2]=0x4000;
    MAP_TRY(map_project(game,matrix,input,axis_y));
    for(i=0;i<3;++i) axis_y[i]=map_sar(axis_y[i],14);
    input[1]=0x4000; input[2]=0;
    MAP_TRY(map_project(game,matrix,input,axis_x));
    for(i=0;i<3;++i) axis_x[i]=map_sar(axis_x[i],14);
    if(state->display_flags&1u) {
        memcpy(scene.position,center,sizeof center); scene.position[2]+=state->depth;
        scene.draw_kind=8; scene.flags=state->display_flags;
        MAP_TRY(MAP_CALL(game,draw_scene,&scene,1));
    }
    state->display_flags|=128u;
    memcpy(projected.position,center,sizeof center);
    MAP_TRY(fe2_map_build_sector(state,game,y,x,&projected,&scene));
    if(state->selection_dirty) {
        uint8_t py=(uint8_t)map_sar((uint32_t)(int32_t)map_s8((uint8_t)((state->center_y>>8)-128u)),1);
        uint8_t px=(uint8_t)map_sar((uint32_t)(int32_t)map_s8((uint8_t)((state->center_x>>8)-128u)),1);
        state->selection_dirty=0; nearest=fe2_map_nearest(&scene,py,px);
        if(nearest<0) {
            if(state->selection_status) {
                MAP_TRY(MAP_CALL(game,primary_panel,0,0)); MAP_TRY(map_reset_menu(state,game));
                state->selection_status=0;
            }
        } else {
            fe2_map_position selected=fe2_map_decode_record(y,x,&scene.records[nearest]);
            int changed=!(state->selection_status&128u) || selected.y!=state->selected.y || selected.x!=state->selected.x || selected.z!=state->selected.z;
            state->selection_status|=128u;
            if(changed) {
                state->selected=selected;
                MAP_TRY(fe2_map_select(state,game,(uint16_t)nearest,scene.identifiers[nearest]));
                MAP_TRY(fe2_map_draw_selection(state,game));
            }
        }
    }
    for(j=0;j<8;++j) {
        vector_add(projected.position,center,axis_y,axis_x,neighbours[j][0],neighbours[j][1]);
        MAP_TRY(fe2_map_build_sector(state,game,(uint16_t)(y+neighbours[j][0]),(uint16_t)(x+neighbours[j][1]),&projected,&scene));
    }
    range=state->marker_range>=3200 ? UINT32_MAX : (uint32_t)state->marker_range*20u;
    MAP_TRY(marker(state,game,matrix,&state->home,24,range));
    if(state->selection_status) MAP_TRY(marker(state,game,matrix,&state->selected,22,range));
    return 0;
}
