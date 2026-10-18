/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "composite.h"
#include "composite_data.h"

int fe2_composite_construct(fe2_composite *state, const fe2_composite_video *video)
{
    if (state==0 || video==0 || video->rectangle==0 || video->palette==0 || video->sprite==0)
        return -1;
    state->appearance=0;
    state->video=*video;
    return 0;
}

int fe2_composite_upload_palette(fe2_composite *state, uint16_t address,
                                  uint8_t start, uint8_t count)
{
    unsigned offset;
    if (state==0 || state->video.palette==0 || address<0xb0u) return -1;
    offset=address-0xb0u;
    if (!composite_rgb6 || offset%3u!=0 || offset+3u*count>composite_rgb6_bytes || (unsigned)start+count>256u)
        return -1;
    return state->video.palette(state->video.opaque,start,count,composite_rgb6+offset);
}

int fe2_composite_draw_part(fe2_composite *state, unsigned style, uint16_t encoded)
{
    unsigned index=encoded/4u;
    const int16_t (*coordinates)[2];
    if (state==0 || state->video.sprite==0 || style>1u || encoded%4u!=0 || index>=44u)
        return -1;
    coordinates=style ? style_b_coordinates : style_a_coordinates;
    return state->video.sprite(state->video.opaque,(uint16_t)((style ? 0xa4u : 0x78u)+index),
        (int16_t)(59+coordinates[index][0]),(int16_t)(136+coordinates[index][1]));
}

int fe2_composite_draw_overlay(fe2_composite *state, uint16_t encoded)
{
    unsigned index=encoded/4u;
    if (state==0 || state->video.sprite==0 || encoded%4u!=0 || index>=3u) return -1;
    return state->video.sprite(state->video.opaque,(uint16_t)(0x6du+index),
        (int16_t)(59+overlay_part_coordinates[index][0]),
        (int16_t)(136+overlay_part_coordinates[index][1]));
}

int fe2_composite_draw_style(fe2_composite *state, unsigned style,
                             uint16_t ax, uint16_t dx, uint16_t override)
{
    static const unsigned shifts[]={1,3,3,5,7,9,13,15};
    static const unsigned masks[]={3,3,3,3,3,15,3,7};
    const uint16_t *groups_a[]={style_a_part_group_5,style_a_part_group_0,
        style_a_part_group_1,style_a_part_group_3,style_a_part_group_2,
        style_a_detail_parts,style_a_part_group_4,style_a_optional_parts};
    const uint16_t *groups_b[]={style_b_part_group_5,style_b_part_group_0,
        style_b_part_group_1,style_b_part_group_3,style_b_part_group_2,
        style_b_detail_parts,style_b_part_group_4,style_b_optional_parts};
    const uint16_t **groups;
    uint32_t packed=((uint32_t)dx<<16)|ax;
    unsigned i;
    if (state==0 || state->video.sprite==0 || style>1u) return -1;
    groups=style ? groups_b : groups_a;
    for (i=0;i<8;++i) {
        int status;
        if (i==7 && (override&6u)!=0) break;
        status=fe2_composite_draw_part(state,style,groups[i][(packed>>shifts[i])&masks[i]]);
        if (status!=0) return status;
    }
    return 0;
}

int fe2_composite_draw(fe2_composite *state, uint16_t ax, uint16_t dx,
                       uint16_t override, fe2_composite_result *result)
{
    uint32_t packed=((uint32_t)dx<<16)|ax;
    uint16_t base;
    unsigned overlay;
    int status;
    if (state==0 || result==0 || state->video.rectangle==0 ||
        state->video.palette==0 || state->video.sprite==0) return -1;
    status=state->video.rectangle(state->video.opaque,1,118,22,136,0);
    if (status!=0) return status;
    state->appearance=(uint8_t)ax;
    base=base_palette_pointers[(packed>>1)&7u];
    status=fe2_composite_upload_palette(state,base,0x80,44);
    if (status!=0) return status;
    status=fe2_composite_upload_palette(state,(uint16_t)(base+132u),0xac,44);
    if (status!=0) return status;
    status=fe2_composite_upload_palette(state,override ? override_palette_pointers[(override>>3)&7u] :
        feature_palette_pointers[(packed>>4)&15u],0xd8,8);
    if (status!=0) return status;
    status=fe2_composite_upload_palette(state,detail_palette_pointers[(packed>>8)&31u],0xe0,8);
    if (status!=0) return status;
    status=fe2_composite_upload_palette(state,secondary_palette_pointers[(packed>>13)&7u],0xe8,8);
    if (status!=0) return status;
    status=fe2_composite_upload_palette(state,feature_palette_pointers[(packed>>16)&15u],0xf0,8);
    if (status!=0) return status;
    status=fe2_composite_upload_palette(state,override ? override_palette_pointers[(override>>3)&7u] :
        feature_palette_pointers[(packed>>20)&15u],0xf8,8);
    if (status!=0) return status;
    /* Public PUSH/POP order exchanges the words before selecting style parts. */
    status=fe2_composite_draw_style(state,state->appearance&1u,dx,ax,override);
    if (status!=0) return status;
    overlay=(override>>1)&3u;
    if (overlay!=0) {
        status=fe2_composite_draw_overlay(state,overlay_part_offsets[overlay-1u]);
        if (status!=0) return status;
    }
    result->ax=dx;
    result->dx=ax;
    return 0;
}
void fe2_composite_noop(void) { }
