/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Status screen: opening, page selection, resuming and initialization. */
#include "status_internal.h"

enum {
    STATUS_PANEL_MODE = 0x0e, STATUS_PANEL = 4, STATUS_PANEL_TITLE = 0x9c09,
    RESUME_LAYOUT = 0x0303, RESUME_BUTTONS = 0x17,
    TEXT_SELECTOR_MASK = 0x03fe
};

/* Preview camera distances per unit of ship size. */
static const int16_t preview_scale[3] = {0x15f, 0x3e, 0x3e8};
static const uint16_t palette_top[3] = {0xb619, 0xb619, 0x49e7};
static const uint16_t palette_ramp[8] = {0x777, 0x777, 0x666, 0x555, 0x444, 0x333, 0x222, 0x111};

typedef void (*page_drawer)(fe2_commander_status *, const fe2_game_services *);

typedef struct page_layout {
    uint8_t page;
    uint16_t controls;
    uint8_t buttons;
    int widgets;
    page_drawer draw;
} page_layout;

static const page_layout pages[] = {
    {FE2_PAGE_PROFILE, 0x1313, 0x13, 0, fe2_status_draw_profile_page},
    {FE2_PAGE_CREW, 0x1b1b, 0x1b, 1, fe2_status_draw_crew_page},
    {FE2_PAGE_CARGO, 0x132b, 0x13, 1, fe2_status_draw_cargo_page},
    {FE2_PAGE_PASSENGERS, 0x1323, 0x03, 0, fe2_status_draw_passenger_page},
    {FE2_PAGE_MINING, 0x2313, 0x13, 1, fe2_status_draw_mining_page},
};
enum { EQUIPMENT_CONTROLS = 0x1313, EQUIPMENT_BUTTONS = 0x23,
       MISSION_CONTROLS = 0x1b13, MISSION_BUTTONS = 0x1b };

/* the page after `page`, skipping lists that are empty. Odd page
 * numbers below 0xc stay valid while any mining machine exists. Its sprite
 * marks the page button. */
static uint8_t following_page(const fe2_commander_status *status)
{
    unsigned page = status->page + 2u;
    if (page == FE2_PAGE_PROFILE) return FE2_PAGE_PROFILE;
    if (page == FE2_PAGE_CREW) {
        if (status->crew_count != 0) return FE2_PAGE_CREW;
        page = FE2_PAGE_CARGO;
    }
    if (page == FE2_PAGE_CARGO) {
        if (status->cargo_used != 0) return FE2_PAGE_CARGO;
        page = FE2_PAGE_PASSENGERS;
    }
    if (page == FE2_PAGE_PASSENGERS) {
        if (status->contract_count != 0) return FE2_PAGE_PASSENGERS;
        page = FE2_PAGE_CONTRACTS;
    }
    if (page == FE2_PAGE_CONTRACTS) {
        if (status->military_count != 0) return FE2_PAGE_CONTRACTS;
        page = FE2_PAGE_MINING;
    }
    if (page <= FE2_PAGE_MINING && status->mining_count != 0) return (uint8_t)page;
    return FE2_PAGE_EQUIPMENT;
}

static void draw_twice_with_preview(fe2_commander_status *status, const fe2_game_services *game,
                                    page_drawer draw)
{
    draw(status, game);
    game->present(game->opaque);
    draw(status, game);
    fe2_status_render_preview(status, game);
}

void fe2_status_redraw_crew_page(fe2_commander_status *status, const fe2_game_services *game)
{
    draw_twice_with_preview(status, game, fe2_status_draw_crew_page);
}

void fe2_status_redraw_cargo_page(fe2_commander_status *status, const fe2_game_services *game)
{
    draw_twice_with_preview(status, game, fe2_status_draw_cargo_page);
}

static int show_equipment_page(fe2_commander_status *status, const fe2_game_services *game)
{
    int result;
    game->set_controls(game->opaque, EQUIPMENT_CONTROLS, EQUIPMENT_BUTTONS);
    status->redraw_flags &= (uint8_t)~1u;
    result = fe2_status_draw_equipment_page(status, game);
    if (result != FE2_STATUS_OK) return result;
    game->present(game->opaque);
    result = fe2_status_draw_equipment_page(status, game);
    if (result != FE2_STATUS_OK) return result;
    fe2_status_render_preview(status, game);
    return FE2_STATUS_OK;
}

static void show_mission_page(fe2_commander_status *status, const fe2_game_services *game)
{
    game->set_controls(game->opaque, MISSION_CONTROLS, MISSION_BUTTONS);
    game->present(game->opaque);
    fe2_status_draw_mission_page(status, game);
    game->present(game->opaque);
    status->redraw_flags &= (uint8_t)~1u;
    fe2_status_draw_mission_page(status, game);
}

/* draw `page`; any page number without a page of its own shows the
 * equipment page. */
int fe2_status_select_page(fe2_commander_status *status, const fe2_game_services *game,
                        uint8_t page)
{
    const page_layout *layout = 0;
    unsigned i;

    status->page = page;
    status->next_page = following_page(status);
    fe2_status_draw_page_sprite(game, status->next_page);
    game->show_panel(game->opaque);
    if (page == FE2_PAGE_CONTRACTS) {
        show_mission_page(status, game);
        return FE2_STATUS_OK;
    }
    for (i = 0; i < sizeof pages / sizeof pages[0]; ++i)
        if (pages[i].page == page) layout = &pages[i];
    if (layout == 0) return show_equipment_page(status, game);
    if (layout->widgets)
        game->set_widgets(game->opaque, (const fe2_widget_table *)fe2_status_widget_bytes);
    game->set_controls(game->opaque, layout->controls, layout->buttons);
    draw_twice_with_preview(status, game, layout->draw);
    return FE2_STATUS_OK;
}


int fe2_status_draw_screen(fe2_commander_status *status, const fe2_game_services *game)
{
    if (!status->screen_active) {
        fe2_call_args args;
        const fe2_ship_type *type;
        unsigned i;

        game->set_panel_mode(game->opaque, STATUS_PANEL_MODE);
        status->screen_active = 1;
        fe2_args_clear(&args);
        fe2_args_word(&args, PARAM_2, STATUS_PANEL_TITLE);
        game->open_panel(game->opaque, STATUS_PANEL, &args);
        for (i = 0; i < 3; ++i) status->palette_top[i] = palette_top[i];
        for (i = 0; i < 8; ++i) status->palette_ramp[i] = palette_ramp[i];
        type = game->ship_type(game->opaque, status->ship->model);
        for (i = 0; i < 3; ++i)
            status->preview_distance[i] = (uint32_t)((int32_t)preview_scale[i] *
                                                     (int16_t)type->size);
    }
    return fe2_status_select_page(status, game, status->next_page);
}

int fe2_status_refresh(fe2_commander_status *status, const fe2_game_services *game)
{
    if (!status->screen_active) return FE2_STATUS_OK;
    return fe2_status_select_page(status, game, status->page);
}


void fe2_status_resume(fe2_commander_status *status, const fe2_game_services *game)
{
    status->screen_active = 0;
    status->redraw_flags |= 1u;
    status->next_page = status->page;
    fe2_status_draw_page_sprite(game, status->next_page);
    game->set_controls(game->opaque, RESUME_LAYOUT, RESUME_BUTTONS);
}


void fe2_status_initialize(fe2_commander_status *status)
{
    status->contract_count = 0;
    status->crew_needed = 0;
    status->crew_count = 0;
    status->next_page = 0;
    status->military_count = 0;
    status->unpaid_wages = 0;
    status->offences[0] = status->offences[1] = status->offences[2] = 0;
}


void fe2_status_format_text(const fe2_game_services *game, uint16_t selector)
{
    game->format_table_text(game->opaque, selector & TEXT_SELECTOR_MASK,
                            (const fe2_text_table *)fe2_status_text_bytes);
}
