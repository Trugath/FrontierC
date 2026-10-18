/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "names.h"
#include <string.h>
#include "names_data.h"

typedef struct name_buffer {
    uint8_t data[FE2_NAME_MAX_BYTES];
    size_t size;
} name_buffer;

static uint32_t rotate(uint32_t value, unsigned count)
{
    return (value<<count)|(value>>(32u-count));
}
static uint32_t swap(uint32_t value)
{
    return (value<<16)|(value>>16);
}
uint16_t fe2_name_sample(fe2_name_seed *seed, uint16_t bound, uint16_t *product_low)
{
    uint32_t total=seed->state+seed->accumulator;
    uint32_t product;
    seed->accumulator=rotate(seed->accumulator,5);
    seed->state=swap(total+seed->accumulator);
    product=(uint32_t)bound*(seed->state&0xffffu);
    if (product_low!=0) *product_low=(uint16_t)product;
    return (uint16_t)(product>>16);
}

static int append(name_buffer *output, uint8_t value)
{
    if (output->size>=sizeof output->data) return FE2_NAME_INVALID;
    output->data[output->size++]=value;
    return FE2_NAME_OK;
}

static int text(name_buffer *output, uint16_t index, const name_buffer *component)
{
    const uint8_t *cursor;
    unsigned at;
    if (!fe2_names_directory || index>=394u || (size_t)index*2u+2u>fe2_names_directory_bytes)
        return FE2_NAME_INVALID;
    at=fe2_names_directory[index*2u]|(unsigned)fe2_names_directory[index*2u+1u]<<8;
    if ((size_t)at>=fe2_names_directory_bytes) return FE2_NAME_INVALID;
    cursor=fe2_names_directory+at;
    while (*cursor!=0) {
        uint8_t value=*cursor++;
        if (value==255u) {
            size_t i;
            if (component==0 || cursor[0]!=0x30u || cursor[1]!=0x16u)
                return FE2_NAME_INVALID;
            cursor+=2;
            for (i=0;i<component->size;++i)
                if (append(output,component->data[i])!=0) return FE2_NAME_INVALID;
        } else if (append(output,value)!=0) return FE2_NAME_INVALID;
    }
    return FE2_NAME_OK;
}

static int suffix(name_buffer *output, fe2_name_seed *seed, int family_only)
{
    uint16_t index;
    if (family_only || (seed->state&255u)>=25u) {
        index=(uint16_t)(89u+fe2_name_sample(seed,138,0));
        return text(output,index,0);
    }
    index=(uint16_t)(79u+fe2_name_sample(seed,10,0));
    if (text(output,index,0)!=0 || append(output,'s')!=0) return FE2_NAME_INVALID;
    if ((seed->state&0x800u)==0)
        if (append(output,'o')!=0 || append(output,'n')!=0) return FE2_NAME_INVALID;
    return FE2_NAME_OK;
}

static int character(name_buffer *output, fe2_name_seed *seed, int letter)
{
    return append(output,(uint8_t)((letter ? 'A' : '0')+
        fe2_name_sample(seed,(uint16_t)(letter ? 26 : 10),0)));
}

int fe2_name_generate(uint16_t mode, uint16_t low_seed, uint16_t high_seed,
                      uint8_t *buffer, size_t capacity, fe2_name_result *result)
{
    uint32_t initial=((uint32_t)high_seed<<16)|low_seed;
    uint32_t swapped=swap(initial);
    fe2_name_seed seed;
    name_buffer output={{0},0};
    if (buffer==0 || result==0) return FE2_NAME_INVALID;
    if (mode<6u) {
        seed.accumulator=rotate(initial+swapped,7);
        seed.state=initial;
        if (mode==4u) {
            (void)fe2_name_sample(&seed,4,0);
            if (suffix(&output,&seed,1)!=0) return FE2_NAME_INVALID;
        } else {
            int alternate=mode==2u || (mode!=1u && (initial&1u)!=0);
            uint16_t index=alternate ? (uint16_t)(224u+fe2_name_sample(&seed,60,0)) :
                                       fe2_name_sample(&seed,105,0);
            if (text(&output,index,0)!=0 || append(&output,' ')!=0 ||
                suffix(&output,&seed,0)!=0) return FE2_NAME_INVALID;
        }
    } else {
        seed.accumulator=rotate(swapped,7);
        seed.state=initial+swapped+seed.accumulator;
        seed.accumulator=rotate(seed.accumulator,4);
        seed.state+=seed.accumulator;
        if (mode<15u) {
            name_buffer component={{0},0};
            uint16_t index;
            if (suffix(&component,&seed,0)!=0 || component.size>=FE2_NAME_COMPONENT_BYTES)
                return FE2_NAME_INVALID;
            index=(uint16_t)(name_bases[mode-6u]+fe2_name_sample(&seed,name_ranges[mode-6u],0));
            if (text(&output,index,&component)!=0) return FE2_NAME_INVALID;
        } else if (mode==15u) {
            if (character(&output,&seed,1)!=0 || character(&output,&seed,1)!=0 ||
                append(&output,'-')!=0 || character(&output,&seed,0)!=0 ||
                character(&output,&seed,0)!=0 || character(&output,&seed,0)!=0)
                return FE2_NAME_INVALID;
        } else if (mode==16u) {
            /* The assembly's unadvanced '+ ' prefix is overwritten by digits. */
            if (character(&output,&seed,0)!=0 || character(&output,&seed,0)!=0 ||
                character(&output,&seed,0)!=0 || character(&output,&seed,1)!=0)
                return FE2_NAME_INVALID;
        } else {
            if (character(&output,&seed,1)!=0 || character(&output,&seed,1)!=0 ||
                append(&output,' ')!=0 || append(&output,'*')!=0 || append(&output,' ')!=0 ||
                character(&output,&seed,0)!=0) return FE2_NAME_INVALID;
        }
    }
    if (append(&output,0)!=0) return FE2_NAME_INVALID;
    if (capacity<output.size) return FE2_NAME_CAPACITY;
    memcpy(buffer,output.data,output.size);
    result->bytes=output.size;
    result->seed=seed;
    return FE2_NAME_OK;
}

int fe2_name_display(uint16_t selector, uint16_t low_seed, uint16_t high_seed,
                     uint8_t *buffer, size_t capacity, fe2_name_result *result)
{
    return fe2_name_generate((uint16_t)((selector&0x3feu)>>1),low_seed,high_seed,
                             buffer,capacity,result);
}
uint16_t fe2_name_end_cursor(uint16_t destination, const fe2_name_result *result)
{
    return (uint16_t)(destination+result->bytes);
}
void fe2_name_noop(void) { }
