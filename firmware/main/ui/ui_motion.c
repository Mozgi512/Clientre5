#include "ui.h"
#include "ui_motion.h"

/* A brief excitation band, with a broad dim tail: positional judder is much
 * less visible than moving all the text at a low frame rate. Only its old/new
 * bounds are invalidated. Time-based progress skips missed frames instead of
 * queueing work. No animation or invalidation remains after the sweep. */
#define SWEEP_MS 360
#define SWEEP_TICK_MS 33
static struct {
    lv_obj_t *screen;
    lv_timer_t *timer;
    lv_area_t region;
    int32_t head, tail;
    uint32_t started;
    bool reverse;
} s_sweep;

static bool intersect(lv_area_t *out, const lv_area_t *a, const lv_area_t *b)
{
    lv_area_t r = { LV_MAX(a->x1, b->x1), LV_MAX(a->y1, b->y1),
                    LV_MIN(a->x2, b->x2), LV_MIN(a->y2, b->y2) };
    *out = r;
    return r.x1 <= r.x2 && r.y1 <= r.y2;
}

static lv_area_t band_bounds(void)
{
    lv_area_t a = s_sweep.region;
    if (s_sweep.reverse) { a.y1 = s_sweep.head; a.y2 = s_sweep.head + s_sweep.tail; }
    else { a.y1 = s_sweep.head - s_sweep.tail; a.y2 = s_sweep.head; }
    return a;
}

static void invalidate_band(void)
{
    if (!s_sweep.screen) return;
    lv_area_t a = band_bounds(), clipped;
    if (intersect(&clipped, &a, &s_sweep.region))
        lv_obj_invalidate_area(s_sweep.screen, &clipped);
}

static void stop_sweep(void)
{
    invalidate_band();
    if (s_sweep.timer) lv_timer_delete(s_sweep.timer);
    s_sweep.timer = NULL;
    s_sweep.screen = NULL;
}

static void sweep_tick(lv_timer_t *timer)
{
    (void)timer;
    uint32_t elapsed = lv_tick_elaps(s_sweep.started);
    if (elapsed >= SWEEP_MS || s_sweep.screen != lv_screen_active()) {
        stop_sweep();
        return;
    }
    invalidate_band();
    int32_t height = lv_area_get_height(&s_sweep.region);
    int32_t progress = (height + s_sweep.tail) * elapsed / SWEEP_MS;
    s_sweep.head = s_sweep.reverse ? s_sweep.region.y2 - progress : s_sweep.region.y1 + progress;
    invalidate_band();
}

static void screen_draw(lv_event_t *e)
{
    lv_obj_t *screen = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_area_t *clip = &layer->_clip_area;
    /* Static phosphor texture: only the invalidated clip, never a timer that
     * repeatedly dirties the entire display just to animate scan lines. */
    if (ui_theme_scanlines()) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_black();
        d.bg_opa = LV_OPA_20;
        for (int32_t y = ((clip->y1 + 3) / 4) * 4; y <= clip->y2; y += 4) {
            lv_area_t a = { clip->x1, y, clip->x2, y };
            lv_draw_rect(layer, &d, &a);
        }
    }
    if (s_sweep.screen != screen) return;
    lv_area_t band = band_bounds(), visible;
    if (!intersect(&visible, &band, &s_sweep.region) ||
        !intersect(&visible, &visible, clip)) return;
    /* Keep the original gradient extent when clipped, otherwise partial draw
     * buffers would restart the tail every 50 lines. Two transparent ends
     * avoid a hard bright edge. No blur, additive blend or temporary layer. */
    int32_t peak = s_sweep.reverse ? band.y1 + s_sweep.tail / 5 : band.y2 - s_sweep.tail / 5;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = UI_ACCENT;
    d.bg_opa = LV_OPA_COVER;
    d.bg_grad.dir = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = d.bg_grad.stops[1].color = UI_ACCENT;
    d.bg_grad.stops[0].frac = 0;
    d.bg_grad.stops[1].frac = 255;
    d.bg_grad.stops[0].opa = LV_OPA_TRANSP;
    d.bg_grad.stops[1].opa = 18;
    lv_area_t part = { band.x1, band.y1, band.x2, peak };
    /* LVGL's layer clip can cover areas beyond the content panel. */
    lv_area_t saved = layer->_clip_area;
    layer->_clip_area = visible;
    lv_draw_rect(layer, &d, &part);
    part.y1 = peak + 1; part.y2 = band.y2;
    d.bg_grad.stops[0].opa = 18;
    d.bg_grad.stops[1].opa = LV_OPA_TRANSP;
    lv_draw_rect(layer, &d, &part);
    layer->_clip_area = saved;
}

static void screen_deleted(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == s_sweep.screen) {
        s_sweep.screen = NULL; /* Never invalidate a screen being destroyed. */
        stop_sweep();
    }
}

void ui_motion_attach(lv_obj_t *screen)
{
    lv_obj_add_event_cb(screen, screen_draw, LV_EVENT_DRAW_POST, NULL);
    lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, NULL);
}

void ui_motion_start(lv_obj_t *screen, lv_obj_t *region, bool reverse)
{
    stop_sweep();
    if (!ui_theme_pixel() || !ui_theme_glow()) return;
    lv_obj_update_layout(screen);
    lv_obj_get_coords(region ? region : screen, &s_sweep.region);
    s_sweep.screen = screen;
    s_sweep.reverse = reverse;
    /* Broad enough to overlap between 30 Hz updates, bounded by panel height. */
    s_sweep.tail = lv_area_get_height(&s_sweep.region) / 5;
    s_sweep.head = reverse ? s_sweep.region.y2 : s_sweep.region.y1;
    s_sweep.started = lv_tick_get();
    s_sweep.timer = lv_timer_create(sweep_tick, SWEEP_TICK_MS, NULL);
    if (!s_sweep.timer) s_sweep.screen = NULL;
}

void ui_motion_load(lv_obj_t *screen, lv_screen_load_anim_t requested)
{
    stop_sweep();
    if (ui_theme_pixel()) {
        /* Never composite two full scenes or translate glyphs in phosphor mode. */
        lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
        if (requested != LV_SCR_LOAD_ANIM_NONE)
            ui_motion_start(screen, NULL, requested == LV_SCR_LOAD_ANIM_MOVE_RIGHT);
    } else {
        lv_screen_load_anim(screen, requested, requested == LV_SCR_LOAD_ANIM_NONE ? 0 : 150, 0, true);
    }
}
