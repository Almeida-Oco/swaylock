#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <wayland-client.h>
#include "cairo.h"
#include "background-image.h"
#include "swaylock.h"

#define IN_SURFACE(desired, max, default) (((desired) <= (max)) ? (desired) : (default))

#define M_PI 3.14159265358979323846
const float TYPE_INDICATOR_RANGE = M_PI / 3.0f;
const float TYPE_INDICATOR_BORDER_THICKNESS = M_PI / 128.0f;

static void set_color_for_state(cairo_t *cairo, struct swaylock_state *state,
		struct swaylock_colorset *colorset) {
	if (state->auth_state == AUTH_STATE_VALIDATING) {
		cairo_set_source_u32(cairo, colorset->verifying);
	} else if (state->auth_state == AUTH_STATE_INVALID) {
		cairo_set_source_u32(cairo, colorset->wrong);
	} else if (state->auth_state == AUTH_STATE_CLEAR) {
		cairo_set_source_u32(cairo, colorset->cleared);
	} else {
		if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
			cairo_set_source_u32(cairo, colorset->caps_lock);
		} else if (state->xkb.caps_lock && !state->args.show_caps_lock_indicator &&
				state->args.show_caps_lock_text) {
			uint32_t inputtextcolor = state->args.colors.text.input;
			state->args.colors.text.input = state->args.colors.text.caps_lock;
			cairo_set_source_u32(cairo, colorset->input);
			state->args.colors.text.input = inputtextcolor;
		} else {
			cairo_set_source_u32(cairo, colorset->input);
		}
	}
}

void render_frame_background(struct swaylock_surface *surface) {
	struct swaylock_state *state = surface->state;

	int buffer_width = surface->width * surface->scale;
	int buffer_height = surface->height * surface->scale;
	if (buffer_width == 0 || buffer_height == 0) {
		return; // not yet configured
	}

	surface->current_buffer = get_next_buffer(state->shm,
			surface->buffers, buffer_width, buffer_height);
	if (surface->current_buffer == NULL) {
		return;
	}

	cairo_t *cairo = surface->current_buffer->cairo;
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, state->args.colors.background);
	cairo_paint(cairo);
	if (surface->image && state->args.mode != BACKGROUND_MODE_SOLID_COLOR) {
		cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
		render_background_image(cairo, surface->image,
			state->args.mode, buffer_width, buffer_height);
	}
	cairo_restore(cairo);
	cairo_identity_matrix(cairo);

	wl_surface_set_buffer_scale(surface->surface, surface->scale);
	wl_surface_attach(surface->surface, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage(surface->surface, 0, 0, surface->width, surface->height);
	wl_surface_commit(surface->surface);
}

void get_datetime(char *date_txt, char *time_txt) {
    time_t rawtime;
    struct tm * timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    sprintf(date_txt, "%02d/%02d/%04d",timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
    sprintf(time_txt, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

void render_frame(struct swaylock_surface *surface) {
	struct swaylock_state *state = surface->state;
    uint32_t mid_x = surface->width / 2, time_y_offset = 0;
    char time_txt[9], date_txt[11];
    get_datetime(date_txt, time_txt);
	int arc_radius = state->args.radius * surface->scale;
	int arc_thickness = state->args.thickness * surface->scale;
	int buffer_diameter = (arc_radius + arc_thickness) * 2;
	int ind_radius = state->args.radius + state->args.thickness;

	wl_subsurface_set_position(surface->subsurface, 0, 0);

	surface->current_buffer = get_next_buffer(state->shm,
			surface->indicator_buffers, surface->width, surface->height);
	if (surface->current_buffer == NULL) {
		return;
	}

	// Hide subsurface until we want it visible
	wl_surface_attach(surface->child, NULL, 0, 0);
	wl_surface_commit(surface->child);

	cairo_t *cairo = surface->current_buffer->cairo;
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
    cairo_font_options_set_subpixel_order(fo, to_cairo_subpixel_order(surface->subpixel));
    cairo_set_font_options(cairo, fo);
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_identity_matrix(cairo);
    cairo_font_options_destroy(fo);

	// Clear
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, 0, 0, 0, 0);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_restore(cairo);

    if (state->args.show_date) {
        cairo_text_extents_t te;
        cairo_font_extents_t fe;
        cairo_set_font_size(cairo, state->args.fonts.date_font_size);
        cairo_text_extents(cairo, date_txt, &te);
        cairo_font_extents(cairo, &fe);
        int date_x = IN_SURFACE(state->args.pos.date_x - te.x_bearing, surface->width, 
                mid_x - (te.width / 2 + te.x_bearing));
        int date_y = IN_SURFACE(state->args.pos.date_y + te.height - (fe.descent / 2),
                 surface->height, 10 + te.height - (fe.descent / 2));
        time_y_offset = fe.height + te.height - (fe.descent / 2);

        printf("Te.x_bearing = %f\n", te.x_bearing);
        cairo_move_to(cairo, date_x, date_y);
        cairo_show_text(cairo, date_txt);

        cairo_new_sub_path(cairo);
    }

    if (state->args.show_time) {
        cairo_text_extents_t te;
        cairo_font_extents_t fe;
        cairo_set_font_size(cairo, state->args.fonts.time_font_size);
        cairo_text_extents(cairo, time_txt, &te);
        cairo_font_extents(cairo, &fe);
        int time_x = IN_SURFACE(state->args.pos.time_x - te.x_bearing, surface->width, 
                mid_x - (te.width / 2 + te.x_bearing));
        int time_y = IN_SURFACE(state->args.pos.time_y + te.height - (fe.descent / 2),
                 surface->height, 10 + time_y_offset + te.height - (fe.descent / 2));

        cairo_move_to(cairo, time_x, time_y);
        cairo_show_text(cairo, time_txt);

        cairo_new_sub_path(cairo);
    }

	float type_indicator_border_thickness =
		TYPE_INDICATOR_BORDER_THICKNESS * surface->scale;

	if (state->args.show_indicator && state->auth_state != AUTH_STATE_IDLE) {
        int ind_x = IN_SURFACE(state->args.pos.ind_x, surface->width, (surface->width / 2) - ind_radius),
            ind_y = IN_SURFACE(state->args.pos.ind_y, surface->height, (surface->height / 2) - ind_radius);
		
        // Draw circle
		cairo_set_line_width(cairo, arc_thickness);
		cairo_arc(cairo, ind_x + buffer_diameter / 2, ind_y + buffer_diameter / 2, arc_radius,
				0, 2 * M_PI);
		set_color_for_state(cairo, state, &state->args.colors.inside);
		cairo_fill_preserve(cairo);
		set_color_for_state(cairo, state, &state->args.colors.ring);
		cairo_stroke(cairo);

		// Draw a message
		char *text = NULL;
		const char *layout_text = NULL;
		char attempts[4]; // like i3lock: count no more than 999
		set_color_for_state(cairo, state, &state->args.colors.text);
		cairo_select_font_face(cairo, state->args.fonts.indicator_font,
				CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		if (state->args.fonts.indicator_font_size > 0) {
		  cairo_set_font_size(cairo, state->args.fonts.indicator_font_size);
		} else {
		  cairo_set_font_size(cairo, arc_radius / 3.0f);
		}
		switch (state->auth_state) {
		case AUTH_STATE_VALIDATING:
			text = "verifying";
			break;
		case AUTH_STATE_INVALID:
			text = "wrong";
			break;
		case AUTH_STATE_CLEAR:
			text = "cleared";
			break;
		case AUTH_STATE_INPUT:
		case AUTH_STATE_INPUT_NOP:
		case AUTH_STATE_BACKSPACE:
			// Caps Lock has higher priority
			if (state->xkb.caps_lock && state->args.show_caps_lock_text) {
				text = "Caps Lock";
			} else if (state->args.show_failed_attempts &&
					state->failed_attempts > 0) {
				if (state->failed_attempts > 999) {
					text = "999+";
				} else {
					snprintf(attempts, sizeof(attempts), "%d", state->failed_attempts);
					text = attempts;
				}
			}

			xkb_layout_index_t num_layout = xkb_keymap_num_layouts(state->xkb.keymap);
			if (!state->args.hide_keyboard_layout && 
					(state->args.show_keyboard_layout || num_layout > 1)) {
				xkb_layout_index_t curr_layout = 0;

				// advance to the first active layout (if any)
				while (curr_layout < num_layout &&
					xkb_state_layout_index_is_active(state->xkb.state,
						curr_layout, XKB_STATE_LAYOUT_EFFECTIVE) != 1) {
					++curr_layout;
				}
				// will handle invalid index if none are active
				layout_text = xkb_keymap_layout_get_name(state->xkb.keymap, curr_layout);
			}
			break;
		default:
			break;
		}

		if (text) {
			cairo_text_extents_t extents;
			cairo_font_extents_t fe;
			double x, y;
			cairo_text_extents(cairo, text, &extents);
			cairo_font_extents(cairo, &fe);
			x = ind_x + (buffer_diameter / 2) -
				(extents.width / 2 + extents.x_bearing);
			y = ind_y + (buffer_diameter / 2) +
				(fe.height / 2 - fe.descent);

			cairo_move_to(cairo, x, y);
			cairo_show_text(cairo, text);
			cairo_close_path(cairo);
			cairo_new_sub_path(cairo);
		}

		// Typing indicator: Highlight random part on keypress
		if (state->auth_state == AUTH_STATE_INPUT
				|| state->auth_state == AUTH_STATE_BACKSPACE) {
			static double highlight_start = 0;
            if (!state->refreshing) {
                highlight_start +=
                    (rand() % (int)(M_PI * 100)) / 100.0 + M_PI * 0.5;
                state->last_highlight = highlight_start;
            }
			cairo_arc(cairo, ind_x + buffer_diameter / 2, ind_y + buffer_diameter / 2,
					arc_radius, highlight_start,
					highlight_start + TYPE_INDICATOR_RANGE);
			if (state->auth_state == AUTH_STATE_INPUT) {
				if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
					cairo_set_source_u32(cairo, state->args.colors.caps_lock_key_highlight);
				} else {
					cairo_set_source_u32(cairo, state->args.colors.key_highlight);
				}
			} else {
				if (state->xkb.caps_lock && state->args.show_caps_lock_indicator) {
					cairo_set_source_u32(cairo, state->args.colors.caps_lock_bs_highlight);
				} else {
					cairo_set_source_u32(cairo, state->args.colors.bs_highlight);
				}
			}
			cairo_stroke(cairo);

			// Draw borders
			cairo_set_source_u32(cairo, state->args.colors.separator);
			cairo_arc(cairo, ind_x + buffer_diameter / 2, ind_y + buffer_diameter / 2,
					arc_radius, highlight_start,
					highlight_start + type_indicator_border_thickness);
			cairo_stroke(cairo);

			cairo_arc(cairo, ind_x + buffer_diameter / 2, ind_y + buffer_diameter / 2,
					arc_radius, highlight_start + TYPE_INDICATOR_RANGE,
					highlight_start + TYPE_INDICATOR_RANGE +
						type_indicator_border_thickness);
			cairo_stroke(cairo);
		}

		// Draw inner + outer border of the circle
		set_color_for_state(cairo, state, &state->args.colors.line);
		cairo_set_line_width(cairo, 2.0 * surface->scale);
		cairo_arc(cairo, ind_x + buffer_diameter / 2, ind_y + buffer_diameter / 2,
				arc_radius - arc_thickness / 2, 0, 2 * M_PI);
		cairo_stroke(cairo);
		cairo_arc(cairo, ind_x + buffer_diameter / 2, ind_y + buffer_diameter / 2,
				arc_radius + arc_thickness / 2, 0, 2 * M_PI);
		cairo_stroke(cairo);

		// display layout text seperately
		if (layout_text) {
			cairo_text_extents_t extents;
			cairo_font_extents_t fe;
			double x, y;
			double box_padding = 4.0 * surface->scale;
			cairo_text_extents(cairo, layout_text, &extents);
			cairo_font_extents(cairo, &fe);
			// upper left coordinates for box
			x = ind_x + (buffer_diameter / 2) - (extents.width / 2) - box_padding;
			y = ind_y + (buffer_diameter / 2) + arc_radius + arc_thickness/2 +
				box_padding; // use box_padding also as gap to indicator

			// background box
			cairo_rectangle(cairo, x, y,
				extents.width + 2.0 * box_padding,
				fe.height + 2.0 * box_padding);
			cairo_set_source_u32(cairo, state->args.colors.layout_background);
			cairo_fill_preserve(cairo);
			// border
			cairo_set_source_u32(cairo, state->args.colors.layout_border);
			cairo_stroke(cairo);
			cairo_new_sub_path(cairo);

			// take font extents and padding into account
			cairo_move_to(cairo,
				x - extents.x_bearing + box_padding,
				y + (fe.height - fe.descent) + box_padding);
			cairo_set_source_u32(cairo, state->args.colors.layout_text);
			cairo_show_text(cairo, layout_text);
			cairo_new_sub_path(cairo);
		}
	}

	wl_surface_set_buffer_scale(surface->child, surface->scale);
	wl_surface_attach(surface->child, surface->current_buffer->buffer, 0, 0);
	wl_surface_damage(surface->child, 0, 0, surface->current_buffer->width, surface->current_buffer->height);
	wl_surface_commit(surface->child);

	wl_surface_commit(surface->surface);
}

void render_frames(struct swaylock_state *state) {
	struct swaylock_surface *surface;
	wl_list_for_each(surface, &state->surfaces, link) {
		render_frame(surface);
	}
}