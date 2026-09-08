static void browser_handle_motion(BrowserState* s, JNIEnv* env, int action,
                                  int x, int y, int w, int h, uint32_t now) {
    int header = w < 360 ? 40 : 46;
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        s->suppress_tap = s->fling_velocity != 0;
        s->fling_velocity = 0;
        s->touching = 1;
        s->touch_down_x = x; s->touch_down_y = y; s->touch_last_y = y;
        s->touch_last_ms = now;
        s->touch_moved = 0; s->sample_count = 0;
        browser_sample(s, y, now);
    } else if (action == AMOTION_EVENT_ACTION_MOVE && s->touching) {
        int dy = y - s->touch_last_y;
        if (y - s->touch_down_y > 8 || s->touch_down_y - y > 8
                || x - s->touch_down_x > 8 || s->touch_down_x - x > 8)
            s->touch_moved = 1;
        if (!s->menu_open && s->touch_down_y >= header && s->touch_moved) {
            s->scroll = iclamp(s->scroll-dy, 0, max_scroll_for(s, w, h));
            s->redraw = 1;
        }
        s->touch_last_y = y; s->touch_last_ms = now;
        browser_sample(s, y, now);
    } else if (action == AMOTION_EVENT_ACTION_UP && s->touching) {
        s->touching = 0;
        if (!s->touch_moved && !s->suppress_tap) {
            if (s->menu_open) {
                int mw = browser_menu_width(w), mx = (w-mw)/2, my = browser_menu_top(w);
                if (x >= mx+8 && x < mx+mw-8 && y >= my+42 && y < my+108) {
                    browser_set_animation(s, !s->animate_previews);
                    browser_save_preferences(s, env);
                } else s->menu_open = 0;
                s->redraw = 1;
            } else if (y < header && x >= browser_header_button_left(w)) {
                s->menu_open = 1; s->redraw = 1;
            } else {
                int idx = item_at(s, w, h, x, y);
                if (idx >= 0) { s->selected = idx; open_item(s, env, idx); }
            }
        } else if (s->touch_moved && !s->menu_open && s->touch_down_y >= header) {
            browser_sample(s, y, now);
            browser_fling_start(s, browser_release_velocity(s, now), monotonicish_millis());
        }
    } else if (action == AMOTION_EVENT_ACTION_CANCEL) {
        s->touching = 0; s->touch_moved = 1; s->fling_velocity = 0;
    }
}
