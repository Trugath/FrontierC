/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "galactic_map_internal.h"
#include "galactic_map_tables.h"
#include <string.h>

int map_text(const fe2_map_services *game,uint16_t selector,const fe2_call_args *args,
              uint8_t colour,uint16_t x,uint16_t y)
{
    char text[FE2_MAP_TEXT_CAPACITY];
    MAP_TRY(fe2_map_format_text(game,selector,0,0,0,0,args,text,sizeof text));
    return MAP_CALL(game,draw_text,text,colour,x,y,0);
}

int map_resident(const fe2_map_services *game,uint16_t selector,const fe2_call_args *args,
                  uint8_t colour,uint16_t x,uint16_t y)
{ return MAP_CALL(game,draw_resident_text,selector,colour,x,y,args); }

int fe2_map_draw_status(const fe2_map_state *state,const fe2_map_services *game)
{
    fe2_call_args args={0}; uint16_t travel,selector=0x84af;
    if(!state) return FE2_MAP_INVALID;
    fe2_args_dword(&args,1,(uint32_t)map_s16((uint16_t)((state->center_y>>16)-0x1718u)));
    fe2_args_dword(&args,2,(uint32_t)map_s16((uint16_t)((state->center_x>>16)-0x1524u)));
    fe2_args_word(&args,7,0);
    MAP_TRY(map_resident(game,0x84ae,&args,state->status_colour,6,157));
    travel=state->travel_result;
    if(!travel) return 0;
    if(travel&32768u) {
        selector=0x84b0; travel&=32767u;
        if(travel&0x4000u) { travel&=(uint16_t)~0x4000u; selector=0x84b1; }
    }
    fe2_args_dword(&args,1,travel);
    return map_resident(game,selector,&args,15,6,147);
}

int fe2_map_draw_selection(const fe2_map_state *state,const fe2_map_services *game)
{
    char text[FE2_MAP_TEXT_CAPACITY]; fe2_call_args args={0};
    if(!state) return FE2_MAP_INVALID;
    MAP_TRY(MAP_CALL(game,primary_panel,1,0)); MAP_TRY(map_button(game,1,256,188));
    MAP_TRY(MAP_CALL(game,draw_text,state->selected_name,state->name_colour,89,170,1));
    MAP_TRY(MAP_CALL(game,draw_text,state->selected_classification,state->classification_colour,80,180,1));
    fe2_args_dword(&args,1,state->selection_distance);
    MAP_TRY(fe2_map_format_text(game,0x13c,0,0,state->selected_index,0,&args,text,sizeof text));
    MAP_TRY(MAP_CALL(game,draw_text,text,state->information_colour,151,170,1));
    MAP_TRY(MAP_CALL(game,format_number,state->detail_result_a,&args,text,sizeof text));
    MAP_TRY(MAP_CALL(game,draw_text,text,state->classification_colour,67,190,1));
    return map_button(game,8,240,186);
}

int fe2_map_draw_system(fe2_map_state *state,const fe2_map_services *game)
{
    fe2_call_args args={0}; fe2_map_query selection;
    if(!state) return FE2_MAP_INVALID;
    MAP_TRY(MAP_CALL(game,set_palette,map_system_palette,128)); state->mode=1;
    MAP_TRY(map_button(game,9,240,186));
    if(!state->detail_status) {
        MAP_TRY(MAP_SIMPLE(game,clear_panel)); fe2_args_word(&args,7,300);
        MAP_TRY(map_resident(game,0x84b3,&args,state->information_colour,10,95));
        return MAP_SIMPLE(game,present);
    }
    fe2_map_cached_selection(state,&selection);
    MAP_TRY(MAP_CALL(game,generate_system,&selection,&state->bodies,&state->named_bodies));
    return map_system_text(state,game);
}

int map_system_text(fe2_map_state *state,const fe2_map_services *game)
{
    fe2_call_args args={0};
    MAP_TRY(fe2_map_draw_hierarchy(state,game));
    fe2_args_dword(&args,1,(uint32_t)map_s8(state->named_bodies));
    MAP_TRY(map_text(game,0x140,&args,state->information_colour,10,95));
    fe2_args_word(&args,7,300);
    MAP_TRY(map_resident(game,state->detail_result_b,&args,state->name_colour,10,115));
    return MAP_SIMPLE(game,present);
}

int fe2_map_draw_trade(fe2_map_state *state,const fe2_map_services *game)
{
    fe2_map_details details={0}; fe2_call_args args={0};
    uint16_t rows[5]={69,69,69,69,69}; unsigned i;
    MAP_TRY(fe2_map_draw_hierarchy(state,game));
    MAP_TRY(map_text(game,0x168,&args,state->status_colour,2,59));
    MAP_TRY(MAP_CALL(game,system_details,state->selection_reference,1,&details));
    for(i=0;i<31;++i) {
        uint8_t code=details.trade[i]; unsigned column; uint16_t x;
        if(code&128u) { column=4; x=256; }
        else if((code&7u)>=5) { column=code&8u ? 2 : 0; x=code&8u ? 128 : 2; }
        else if((code&7u)>=3) { column=code&8u ? 3 : 1; x=code&8u ? 193 : 65; }
        else continue;
        MAP_TRY(map_resident(game,(uint16_t)(0x8e00+i),&args,state->information_colour,x,rows[column]));
        rows[column]=(uint16_t)(rows[column]+9);
    }
    return MAP_SIMPLE(game,present);
}

int fe2_map_draw_government(fe2_map_state *state,const fe2_map_services *game)
{
    fe2_map_details trade={0},details={0}; fe2_call_args args={0};
    uint16_t y,x;
    MAP_TRY(fe2_map_draw_hierarchy(state,game));
    MAP_TRY(MAP_CALL(game,system_details,state->selection_reference,1,&trade));
    MAP_TRY(MAP_CALL(game,system_details,(state->selection_reference&UINT32_C(0xffff0000))|trade.result_a,0,&details));
    fe2_args_word(&args,1,(uint16_t)(0x84c9+(details.government&63u)));
    fe2_args_word(&args,2,(uint16_t)(0x84ba+(details.government&63u)));
    fe2_args_word(&args,3,(uint16_t)(0x84b7+(details.government>>6)));
    fe2_args_word(&args,10,(uint16_t)(0x84d8+(trade.population_text&255u)));
    MAP_TRY(map_text(game,0x16a,&args,state->information_colour,40,99));
    y=(uint16_t)((state->selection_reference>>6)&8191u); x=(uint16_t)(state->selection_reference>>19);
    fe2_args_dword(&args,3,state->selection_reference&63u);
    fe2_args_dword(&args,2,(uint32_t)map_s16((uint16_t)(x-0x1524u)));
    fe2_args_dword(&args,1,(uint32_t)map_s16((uint16_t)(y-0x1718u)));
    MAP_TRY(map_text(game,0x16c,&args,state->information_colour,40,139));
    return MAP_SIMPLE(game,present);
}

static int record_name(const fe2_system_record *record,char *output,size_t capacity)
{
    const char *name=fe2_record_name_const(record); size_t n=0;
    while(n<FE2_RECORD_NAME_BYTES && name[n]) ++n;
    if(n==FE2_RECORD_NAME_BYTES) return FE2_MAP_INVALID;
    if(n>=capacity) return FE2_MAP_BUFFER_SMALL;
    memcpy(output,name,n+1); return 0;
}

static int flow(const fe2_map_services *game,const char *text,uint8_t colour,uint16_t *x,uint16_t *y)
{
    if(!game || !game->draw_text_flow) return FE2_MAP_MISSING_SERVICE;
    return game->draw_text_flow(game->opaque,text,colour,x,y);
}

int fe2_map_draw_body(fe2_map_state *state,const fe2_map_services *game,unsigned index)
{
    const fe2_system_record *body;
    uint16_t kind,mantissa,exponent,x=10,y; uint32_t value;
    fe2_call_args args={0}; char text[FE2_MAP_TEXT_CAPACITY];
    if(!state || index>=state->bodies.count) return FE2_MAP_INVALID;
    MAP_TRY(fe2_map_draw_hierarchy(state,game)); body=&state->bodies.records[index];
    kind=fe2_record_word(body,FE2_RECORD_KIND);
    if(kind && map_s16(kind)<64) {
        uint64_t mass;
        mantissa=fe2_record_word(body,FE2_RECORD_SIZE_MANTISSA);
        exponent=(uint16_t)(fe2_record_word(body,FE2_RECORD_SIZE_EXPONENT)-63u);
        MAP_TRY(MAP_CALL(game,body_number,mantissa,exponent,0,&value));
        mass=(uint64_t)value*0xab5au;
        fe2_args_dword(&args,2,(uint32_t)(mass>>16)>>2);
        fe2_args_dword(&args,1,(uint32_t)map_s16((uint16_t)(fe2_record_word(body,FE2_RECORD_TEMPERATURE)-273u)));
        MAP_TRY(map_text(game,0x142,&args,state->information_colour,10,105));
        MAP_TRY(fe2_map_format_text(game,0x14e,0,0,0,0,&args,text,sizeof text));
        if(index+1<state->bodies.count && fe2_record_word(&state->bodies.records[index+1],FE2_RECORD_KIND)==FE2_RECORD_KIND_VERBATIM) {
            size_t at=0; unsigned i;
            for(i=index+1;i<state->bodies.count && fe2_record_word(&state->bodies.records[i],FE2_RECORD_KIND)==FE2_RECORD_KIND_VERBATIM;++i) {
                if(i!=index+1) { if(at+1>=sizeof text) return FE2_MAP_BUFFER_SMALL; text[at++]=','; }
                MAP_TRY(record_name(&state->bodies.records[i],text+at,sizeof text-at)); at+=strlen(text+at);
            }
        }
        MAP_TRY(MAP_CALL(game,draw_text,text,state->information_colour,82,125,0));
    }
    mantissa=fe2_record_word(body,FE2_RECORD_DERIVED_MANTISSA);
    exponent=fe2_record_word(body,FE2_RECORD_DERIVED_EXPONENT);
    if(mantissa) {
        uint16_t selector=0x148,factor=0x48d1,factor_exp=0xfff5,parent;
        if(map_s16(exponent)>27) { selector=0x14c; factor=0x551f; factor_exp=0xffeb; }
        else if(map_s16(exponent)>19) { selector=0x14a; factor=0x6117; factor_exp=0xfff0; }
        MAP_TRY(MAP_CALL(game,body_scale,mantissa,exponent,factor,factor_exp,&mantissa,&exponent));
        MAP_TRY(MAP_CALL(game,body_number,mantissa,exponent,0,&value)); fe2_args_dword(&args,2,value);
        MAP_TRY(map_text(game,selector,&args,state->information_colour,82,135));
        MAP_TRY(MAP_CALL(game,body_scale,fe2_record_word(body,FE2_RECORD_SCALAR_MANTISSA),
            fe2_record_word(body,FE2_RECORD_SCALAR_EXPONENT),0x759a,0xffdb,&mantissa,&exponent));
        MAP_TRY(MAP_CALL(game,body_number,mantissa,exponent,1,&value)); fe2_args_dword(&args,1,value);
        fe2_args_dword(&args,2,(uint32_t)map_s16(fe2_record_word(body,FE2_RECORD_ORBIT_ECCENTRICITY)));
        fe2_args_dword(&args,3,(uint32_t)map_s16(((uint32_t)3600*fe2_record_word(body,FE2_RECORD_ORBIT_INCLINATION))>>16));
        parent=fe2_record_word(body,FE2_RECORD_PARENT); selector=0x144;
        if((!parent || (parent<=state->bodies.count && !fe2_record_word(&state->bodies.records[parent-1],FE2_RECORD_KIND))) &&
            map_s16((uint16_t)((index-parent)*64u))<=65) { selector=0x146; fe2_args_dword(&args,1,value<<1); }
        MAP_TRY(map_text(game,selector,&args,state->information_colour,10,135));
    }
    y=!kind || (uint8_t)kind>=64 ? 125 : 95;
    MAP_TRY(record_name(body,text,sizeof text)); MAP_TRY(flow(game,text,state->information_colour,&x,&y));
    MAP_TRY(flow(game,map_body_separator,state->information_colour,&x,&y));
    MAP_TRY(fe2_map_format_text(game,(uint16_t)(kind+0xec),0,0,0,0,&args,text,sizeof text));
    MAP_TRY(flow(game,text,state->information_colour,&x,&y));
    return MAP_SIMPLE(game,present);
}
