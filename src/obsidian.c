#include <pebble.h>

// I don't know how to pass parameters to the compiler, so I'm using this file
// for various configurations
#include "config.h"


////////////////////////////////////////////
//// Configuration constants
////////////////////////////////////////////

#define COLOR_BATTERY_WARNING_BACKGROUND_1 GColorYellow
#define COLOR_BATTERY_WARNING_BACKGROUND_2 GColorOrange
#define COLOR_BATTERY_WARNING_BACKGROUND_3 GColorRed
#define COLOR_BACKGROUND GColorWhite
#define COLOR_BACKGROUND_OUTER GColorDarkGray
#define COLOR_NORMAL GColorBlack
//#define COLOR_ACCENT GColorBlue
#define COLOR_ACCENT GColorJaegerGreen
#define COLOR_BATTERY GColorBlack

//#define OBSIDIAN_SHOW_NUMBERS
#define OBSIDIAN_LONG_TICKS
#define OBSIDIAN_FAT_TICKS
//#define OBSIDIAN_ONLY_RELEVANT_MINUTE_TICKS
//#define OBSIDIAN_ONLY_RELEVANT_NUMBER
//#define OBSIDIAN_BATTERY_USE_TEXT


////////////////////////////////////////////
//// Global variables
////////////////////////////////////////////

/** A pointer to our window, for later deallocation. */
static Window *window;

/** All layers */
static Layer *layer_background;

/** Buffers for strings */
static char buffer_1[30];
static char buffer_2[30];

/** The center of the watch */
static GPoint center;

#ifdef OBSIDIAN_SHOW_NUMBERS
/** Open Sans font. */
static GFont font_open_sans;
#endif

/** System font. */
static GFont font_system_18px_bold;


////////////////////////////////////////////
//// Implementation
////////////////////////////////////////////

#ifdef DEBUG_MENU_ICON
#define DEBUG_NICE_TIME
#define DEBUG_NO_DATE
#define DEBUG_NO_BATTERY_ICON
#endif

/**
 * Returns a point on the line from the center away at an angle specified by tick/maxtick, at a specified distance
 */
static GPoint get_radial_point(const int16_t distance_from_center, const int32_t angle) {
    GPoint result = {
            .x = (int16_t) (sin_lookup(angle) * (int32_t) distance_from_center / TRIG_MAX_RATIO) + center.x,
            .y = (int16_t) (-cos_lookup(angle) * (int32_t) distance_from_center / TRIG_MAX_RATIO) + center.y,
    };
    return result;
}

/**
 * Do the line segments a0->a1 and b0->b1 intersect?
 * Loosely based on http://stackoverflow.com/questions/4977491/determining-if-two-line-segments-intersect/4977569#4977569
 */
static bool intersect(const GPoint a0, const GPoint a1, const GPoint b0, const GPoint b1) {
    GPoint va = GPoint(a1.x - a0.x, a1.y - a0.y);
    GPoint vb = GPoint(b1.x - b0.x, b1.y - b0.y);

    // test for parallel line segments
    int16_t det = vb.x * va.y - va.x * vb.y;
    if (det == 0) {
        if ((b0.x - a0.x) * va.y == (b0.y - a0.y) * va.x) {
            // the two lines are parallel, and might overlap (if the segments were extended infinitely, they would be the same line)
            return (0 <= b0.x - a0.x && b0.x - a0.x <= va.x) || (0 <= a0.x - b0.x && a0.x - b0.x <= vb.x);
        } else {
            // the two lines are parallel, and not overlapping
            return false;
        }
    }

    int16_t s = ((a0.x - b0.x) * va.y - (a0.y - b0.y) * va.x);
    int16_t t = -(-(a0.x - b0.x) * vb.y + (a0.y - b0.y) * vb.x);

    if (det < 0) {
        det = -det;
        s = -s;
        t = -t;
    }
    return 0 <= s && s <= det && 0 <= t && t <= det;
}

/**
 * Returns a point on the line from the center away at an angle specified by tick/maxtick, at a specified distance
 */
static GPoint get_radial_point_basic(const int16_t distance_from_center, const int32_t tick,
                                     const int32_t maxtick) {
    return get_radial_point(distance_from_center, TRIG_MAX_ANGLE * tick / maxtick);
}

/**
 * Draws a bluetooth logo at a given position (top-left).
 */
static void draw_bluetooth_logo(GContext *ctx, GPoint origin) {
#define BLUETOOTH_LOGO_STEP 3

    // background
    graphics_context_set_fill_color(ctx, COLOR_ACCENT);
    graphics_fill_rect(ctx, GRect(origin.x - 2, origin.y - 2, BLUETOOTH_LOGO_STEP * 2 + 5, BLUETOOTH_LOGO_STEP * 4 + 5),
                       2, GCornersAll);

    // logo on the inside
    graphics_context_set_antialiased(ctx, false);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 1);

    graphics_draw_line(ctx, GPoint(origin.x + BLUETOOTH_LOGO_STEP, origin.y + 0),
                       GPoint(origin.x + BLUETOOTH_LOGO_STEP, origin.y + 4 * BLUETOOTH_LOGO_STEP));

    graphics_draw_line(ctx, GPoint(origin.x + 0, origin.y + BLUETOOTH_LOGO_STEP),
                       GPoint(origin.x + 2 * BLUETOOTH_LOGO_STEP, origin.y + 3 * BLUETOOTH_LOGO_STEP));
    graphics_draw_line(ctx, GPoint(origin.x + 0, origin.y + 3 * BLUETOOTH_LOGO_STEP),
                       GPoint(origin.x + 2 * BLUETOOTH_LOGO_STEP, origin.y + BLUETOOTH_LOGO_STEP));

    graphics_draw_line(ctx, GPoint(origin.x + BLUETOOTH_LOGO_STEP, origin.y + 0),
                       GPoint(origin.x + 2 * BLUETOOTH_LOGO_STEP, origin.y + BLUETOOTH_LOGO_STEP));
    graphics_draw_line(ctx, GPoint(origin.x + BLUETOOTH_LOGO_STEP, origin.y + 4 * BLUETOOTH_LOGO_STEP),
                       GPoint(origin.x + 2 * BLUETOOTH_LOGO_STEP, origin.y + 3 * BLUETOOTH_LOGO_STEP));
}

/**
 * Returns true if the line and rectangle intersect.
 */
bool line_rect_intersect(GPoint line0, GPoint line1, GPoint rect0, GPoint rect1) {
    return intersect(line0, line1, rect0, GPoint(rect1.x, rect0.y)) ||
           intersect(line0, line1, rect0, GPoint(rect0.x, rect1.y)) ||
           intersect(line0, line1, rect1, GPoint(rect1.x, rect0.y)) ||
           intersect(line0, line1, rect1, GPoint(rect0.x, rect1.y));
}

/**
 * Returns true if either of two lines intersects with the rectangle.
 */
bool line2_rect_intersect(GPoint lineA0, GPoint lineA1, GPoint lineB0, GPoint lineB1, GPoint rect0, GPoint rect1) {
    return line_rect_intersect(lineA0, lineA1, rect0, rect1) || line_rect_intersect(lineB0, lineB1, rect0, rect1);
}

//#define DEBUG_DATE_POSITION
#ifdef DEBUG_DATE_POSITION
static int debug_iter = 0;
#endif

/**
 * Update procedure for the background
 */
static void background_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    int16_t radius = bounds.size.w / 2;
    bool bluetooth = bluetooth_connection_service_peek();
    BatteryChargeState battery_state = battery_state_service_peek();
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // background
    GColor8 outer_color = COLOR_BACKGROUND_OUTER;
    if (battery_state.charge_percent <= 10) {
        outer_color = COLOR_BATTERY_WARNING_BACKGROUND_3;
    } else if (battery_state.charge_percent <= 20) {
        outer_color = COLOR_BATTERY_WARNING_BACKGROUND_2;
    } else if (battery_state.charge_percent <= 30) {
        outer_color = COLOR_BATTERY_WARNING_BACKGROUND_1;
    }

    graphics_context_set_fill_color(ctx, outer_color);
    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

    // battery
//    const int max_battery_points = 50;
//    graphics_context_set_stroke_color(ctx, COLOR_NORMAL);
//    graphics_context_set_fill_color(ctx, COLOR_NORMAL);
//    graphics_context_set_stroke_width(ctx, 1);
//    const int cur_battery_points = battery_state_service_peek().charge_percent * max_battery_points / 100;
//    for (int i = 0; i < cur_battery_points; ++i) {
//        int32_t angle = i * TRIG_MAX_ANGLE / max_battery_points;
////        graphics_draw_line(ctx, get_radial_point(radius, angle), get_radial_point(radius + 9, angle));
//        graphics_fill_circle(ctx, get_radial_point(radius+5, angle), 3);
//    }

    // background
    graphics_context_set_fill_color(ctx, COLOR_BACKGROUND);
    graphics_fill_circle(ctx, center, radius);
    graphics_context_set_stroke_color(ctx, COLOR_NORMAL);
    graphics_context_set_stroke_width(ctx, 4);
    graphics_draw_circle(ctx, center, radius + 3);

    // numbers
#if defined(OBSIDIAN_SHOW_NUMBERS) || defined(OBSIDIAN_ONLY_RELEVANT_NUMBER)
    static const GPoint number_points[] = {
            {144 / 2 - 9,       26}, // 12
            {144 / 2 + 23,      28}, // 1
            {144 / 2 + 45,      47}, // 2
            {144 / 2 + 49,      77}, // 3
            {144 / 2 + 45,      102}, // 4
            {144 / 2 + 24,      124}, // 5
            {144 / 2 - 4,       128}, // 6
            {144 / 2 - 6 - 23,  124}, // 7
            {144 / 2 - 6 - 43,  103}, // 8
            {144 / 2 - 6 - 51,  77}, // 9
            {144 / 2 - 12 - 43, 48}, // 10
            {144 / 2 - 12 - 23, 28}, // 11
    };
    static const char *numbers[] = {
            "12",
            "1",
            "2",
            "3",
            "4",
            "5",
            "6",
            "7",
            "8",
            "9",
            "10",
            "11",
    };
    graphics_context_set_text_color(ctx, GColorDarkGray);
#ifdef OBSIDIAN_ONLY_RELEVANT_NUMBER
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int i = t->tm_hour % 12;
#else
    for (unsigned i = 0; i < ARRAY_LENGTH(number_points); i++) {
#endif
        graphics_draw_text(ctx, numbers[i], font_open_sans,
                           GRect(number_points[i].x, number_points[i].y, strlen(numbers[i]) > 1 ? 18 : 9, 22),
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
#endif

    // hour ticks
    graphics_context_set_stroke_color(ctx, COLOR_NORMAL);
    graphics_context_set_stroke_width(ctx, 2);
    for (int i = 0; i < 12; ++i) {
        int32_t angle = i * TRIG_MAX_ANGLE / 12;
        int tick_length = 6;
#ifdef OBSIDIAN_LONG_TICKS
        if (i % 3 == 0) {
            tick_length = 10;
#ifdef OBSIDIAN_FAT_TICKS
            graphics_context_set_stroke_width(ctx, 4);
        } else {
            graphics_context_set_stroke_width(ctx, 2);
#endif
        }
#endif
        graphics_draw_line(ctx, get_radial_point(radius, angle), get_radial_point(radius - tick_length, angle));
    }

#ifdef OBSIDIAN_ONLY_RELEVANT_MINUTE_TICKS
    // only relevant minute ticks
    int start_min_tick = (t->tm_min / 5) * 5;
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = start_min_tick; i < start_min_tick + 5; ++i) {
        int32_t angle = i * TRIG_MAX_ANGLE / 60;
        graphics_draw_line(ctx, get_radial_point(radius, angle), get_radial_point(radius - 3, angle));
    }
#else
    // all minute ticks
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 60; ++i) {
        int32_t angle = i * TRIG_MAX_ANGLE / 60;
        graphics_draw_line(ctx, get_radial_point(radius, angle), get_radial_point(radius - 3, angle));
    }
#endif

#ifdef DEBUG_DATE_POSITION
    // for testing only
    t->tm_min = 2;
    t->tm_hour = 1;
#endif
#ifdef DEBUG_NICE_TIME
    t->tm_min = 10;
    t->tm_hour = 10;
#endif

    // compute angles
    int32_t minute_angle = t->tm_min * TRIG_MAX_ANGLE / 60;
    GPoint minute_hand = get_radial_point(radius - 10, minute_angle);
    int hour_tick = ((t->tm_hour % 12) * 6) + (t->tm_min / 10);
    int32_t hour_angle = hour_tick * TRIG_MAX_ANGLE / (12 * 6);
    GPoint hour_hand = get_radial_point(radius * 55 / 100, hour_angle);

    // format date strings
    strftime(buffer_1, sizeof(buffer_1), "%b %d", t);
    // remove leading zeros
    if (buffer_1[4] == '0') {
        memcpy(&buffer_1[4], &buffer_1[5], 2);
    }
    strftime(buffer_2, sizeof(buffer_2), "%a", t);

    // determine where we can draw the date without overlap
    const GPoint d_points[] = {
            // array of candidate points to draw the date strings at
            GPoint(0, 0),
            GPoint(10, -3),
            GPoint(17, -7),
            GPoint(26, -13),
            GPoint(29, -19),
            GPoint(33, -25),
            GPoint(33, -32),
            GPoint(33, -39),
    };
    const int d_offset = 15;
    const int d_height = 21;
    const int d_y_start = 100;
    bool found = false;
    uint16_t i;
    GPoint d_center;
    GRect date_pos;
    GRect day_pos;
    // loop through all points and use the first one that doesn't overlap with the watch hands
    for (i = 0; i < 1 + (ARRAY_LENGTH(d_points) - 1) * 2; i++) {
        d_center = d_points[(i + 1) / 2];
        if (i % 2 == 0) {
            d_center.x = -d_center.x;
        }
        date_pos = GRect(d_center.x, d_y_start + d_center.y + d_offset, 144, d_height);
        GSize date_size = graphics_text_layout_get_content_size(buffer_1, font_system_18px_bold, date_pos,
                                                                GTextOverflowModeWordWrap, GTextAlignmentCenter);
        day_pos = GRect(d_center.x, d_y_start + d_center.y, 144, d_height);
        GSize day_size = graphics_text_layout_get_content_size(buffer_2, font_system_18px_bold, day_pos,
                                                               GTextOverflowModeWordWrap, GTextAlignmentCenter);
        if (!(line2_rect_intersect(center, hour_hand, center, minute_hand,
                                   GPoint(72 + d_center.x - day_size.w / 2, d_y_start + d_center.y),
                                   GPoint(72 + d_center.x + day_size.w / 2, d_y_start + d_center.y + d_height)) ||
              line2_rect_intersect(center, hour_hand, center, minute_hand,
                                   GPoint(72 + d_center.x - date_size.w / 2, d_y_start + d_center.y + d_offset),
                                   GPoint(72 + d_center.x + date_size.w / 2,
                                          d_y_start + d_center.y + d_height + d_offset)))) {
            found = true;
            break;
        }
    }

    // this should not happen, but if it does, then use the default position
    if (!found) {
        d_center = d_points[0];
        date_pos = GRect(d_center.x, d_y_start + d_center.y + d_offset, 144, d_height);
        day_pos = GRect(d_center.x, d_y_start + d_center.y, 144, d_height);
    }

    // actuallyl draw the date text
#ifndef DEBUG_NO_DATE
    graphics_context_set_text_color(ctx, COLOR_NORMAL);
    graphics_draw_text(ctx, buffer_2, font_system_18px_bold, day_pos, GTextOverflowModeWordWrap, GTextAlignmentCenter,
                       NULL);
    graphics_context_set_text_color(ctx, COLOR_ACCENT);
    graphics_draw_text(ctx, buffer_1, font_system_18px_bold, date_pos, GTextOverflowModeWordWrap, GTextAlignmentCenter,
                       NULL);
#endif

    // second hand
//    GPoint second_hand = get_radial_point_basic(radius, t->tm_sec, 60);
//    graphics_context_set_stroke_width(ctx, 4);
//    graphics_context_set_stroke_color(ctx, GColorBlack);
//    graphics_draw_line(ctx, second_hand, center);
//    graphics_context_set_stroke_width(ctx, 3);
//    graphics_context_set_stroke_color(ctx, GColorWhite);
//    graphics_draw_line(ctx, second_hand, center);

    // minute hand
    graphics_context_set_stroke_width(ctx, 5);
    graphics_context_set_stroke_color(ctx, COLOR_BACKGROUND);
    graphics_draw_line(ctx, minute_hand, center);
    graphics_context_set_stroke_width(ctx, 4);
    graphics_context_set_stroke_color(ctx, COLOR_NORMAL);
    graphics_draw_line(ctx, minute_hand, center);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_line(ctx, get_radial_point(radius - 12, minute_angle), center);

    // hour hand
    graphics_context_set_stroke_width(ctx, 5);
    graphics_context_set_stroke_color(ctx, COLOR_BACKGROUND);
    graphics_draw_line(ctx, hour_hand, center);
    graphics_context_set_stroke_width(ctx, 4);
    graphics_context_set_stroke_color(ctx, COLOR_ACCENT);
    graphics_draw_line(ctx, hour_hand, center);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_line(ctx, get_radial_point(radius * 55 / 100 - 2, hour_angle), center);

    // dot in the middle
    graphics_context_set_fill_color(ctx, COLOR_NORMAL);
    graphics_fill_circle(ctx, center, 5);
    graphics_context_set_fill_color(ctx, COLOR_BACKGROUND);
    graphics_fill_circle(ctx, center, 2);

    // bluetooth status
    if (!bluetooth) {
        draw_bluetooth_logo(ctx, GPoint(144 / 2 - 3, 40));
    }

    // battery status
#ifndef DEBUG_NO_BATTERY_ICON
#ifdef OBSIDIAN_BATTERY_USE_TEXT
    const GRect battery = GRect(118, 2, 22, 11);
    snprintf(buffer_1, sizeof(buffer_1), "%d", battery_state.charge_percent);//battery_state.charge_percent);
    graphics_context_set_text_color(ctx, COLOR_BATTERY);
    graphics_draw_text(ctx, buffer_1, font_open_sans, GRect(battery.origin.x, battery.origin.y-1, battery.size.w, battery.size.h),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    graphics_context_set_stroke_color(ctx, COLOR_BATTERY);
    graphics_draw_rect(ctx, GRect(battery.origin.x, battery.origin.y, battery.size.w, battery.size.h));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(battery.origin.x + battery.size.w, battery.origin.y + 3),
                       GPoint(battery.origin.x + battery.size.w, battery.origin.y + battery.size.h - 3));
#else
    const GRect battery = GRect(125, 3, 14, 8);
    graphics_context_set_stroke_color(ctx, COLOR_BATTERY);
    graphics_context_set_fill_color(ctx, COLOR_BATTERY);
    graphics_draw_rect(ctx, battery);
    graphics_fill_rect(ctx, GRect(battery.origin.x + 2, battery.origin.y + 2, battery_state.charge_percent / 10, 4), 0,
                       GCornerNone);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, GPoint(battery.origin.x + battery.size.w, battery.origin.y + 2),
                       GPoint(battery.origin.x + battery.size.w, battery.origin.y + 5));
#endif
#endif
//    graphics_context_set_fill_color(ctx, COLOR_BACKGROUND_OUTER);
//    graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
//    graphics_context_set_fill_color(ctx, COLOR_BACKGROUND);
//    graphics_context_set_stroke_color(ctx, COLOR_NORMAL);
//    graphics_context_set_stroke_width(ctx, 4);
//    GRect notification_rect = GRect(10, 40, 144 - 2 * 10, 168 - 2 * 40);
//    graphics_fill_rect(ctx, notification_rect, 8, GCornersAll);
//    graphics_draw_round_rect(ctx, notification_rect, 8);
//    graphics_context_set_text_color(ctx, COLOR_NORMAL);
//    graphics_draw_text(ctx, "Bluetooth Connected", font_system_18px_bold,
//                       GRect(notification_rect.origin.x+10, notification_rect.origin.y + 10, notification_rect.size.w-20, 70),
//                       GTextOverflowModeWordWrap, GTextAlignmentCenter,
//                       NULL);
//    GPoint notification_logo = GPoint(144/2-3, notification_rect.origin.y + notification_rect.size.h - 28);
//    draw_bluetooth_logo(ctx, notification_logo);
//    graphics_context_set_stroke_color(ctx, GColorRed);
//    graphics_context_set_stroke_width(ctx, 2);
//    graphics_draw_circle(ctx, GPoint(144/2, notification_logo.y + 6), 12);
//    graphics_draw_line(ctx, GPoint(144/2-9, notification_logo.y + 2), GPoint(144/2+9, notification_logo.y + 12 - 2));
}

static void handle_battery(BatteryChargeState new_state) {
    layer_mark_dirty(layer_background);
}

/**
 * Handler for time ticks.
 */
static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
    layer_mark_dirty(layer_background);
#ifdef DEBUG_DATE_POSITION
    debug_iter += 1;
#endif
}

static void handle_bluetooth(bool connected) {
    // redraw background (to turn on/off the logo)
    layer_mark_dirty(layer_background);

    // vibrate
    vibes_double_pulse();

    // turn light on
    light_enable_interaction();
}

/**
 * Window load callback.
 */
static void window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // create layer
    layer_background = layer_create(bounds);
    layer_set_update_proc(layer_background, background_update_proc);
    layer_add_child(window_layer, layer_background);

    // load fonts
#ifdef OBSIDIAN_SHOW_NUMBERS
    font_open_sans = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_OPEN_SANS_12));
#endif
    font_system_18px_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
}

/**
 * Window unload callback.
 */
static void window_unload(Window *window) {
    layer_destroy(layer_background);
#ifdef OBSIDIAN_SHOW_NUMBERS
    fonts_unload_custom_font(font_open_sans);
#endif
}

/**
 * Initialization.
 */
static void init() {
    window = window_create();
    window_set_window_handlers(window, (WindowHandlers) {
            .load = window_load,
            .unload = window_unload,
    });
    window_stack_push(window, true);

    GRect bounds = layer_get_bounds(window_get_root_layer(window));
    center = grect_center_point(&bounds);

    TimeUnits unit = MINUTE_UNIT;
#ifdef DEBUG_DATE_POSITION
    unit = SECOND_UNIT;
#endif
    tick_timer_service_subscribe(unit, handle_second_tick);
    battery_state_service_subscribe(handle_battery);
    bluetooth_connection_service_subscribe(handle_bluetooth);
}

/**
 * De-initialisation.
 */
static void deinit() {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    bluetooth_connection_service_unsubscribe();

    window_destroy(window);
}

/**
 * Main entry point.
 */
int main() {
    init();
    app_event_loop();
    deinit();
}
