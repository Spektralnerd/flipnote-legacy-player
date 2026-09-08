/* Integer-only browser settings and fling model, shared by API 7 and API 9 hosts. */
static int browser_menu_width(int width) { return imin(width - 16, 520); }
static int browser_menu_top(int width) { return (width < 360 ? 40 : 46) + 12; }
static int browser_header_button_left(int width) { return imax(0, width - 110); }

static void browser_set_animation(BrowserState* s, int enabled) {
    if (s->animate_previews == !!enabled) return;
    s->animate_previews = !!enabled;
    /* Drop complete PPMs and decoded layers; static mode is not just a pause. */
    free_thumbnails(s);
    s->redraw = 1;
}

static void browser_sample(BrowserState* s, int y, uint32_t time) {
    int i;
    if (s->sample_count && time == s->sample_time[s->sample_count - 1]) {
        s->sample_y[s->sample_count - 1] = y;
        return;
    }
    if (s->sample_count == 8) {
        for (i = 0; i < 7; ++i) {
            s->sample_time[i] = s->sample_time[i+1];
            s->sample_y[i] = s->sample_y[i+1];
        }
        --s->sample_count;
    }
    s->sample_time[s->sample_count] = time;
    s->sample_y[s->sample_count++] = y;
}

static int browser_release_velocity(BrowserState* s, uint32_t time) {
    int last = s->sample_count - 1, first;
    uint32_t dt;
    if (last < 1 || time - s->sample_time[last] > 100u) return 0;
    first = last;
    while (first > 0 && time - s->sample_time[first-1] <= 120u) --first;
    dt = time - s->sample_time[first];
    if (dt < 10u || dt > 180u) return 0;
    return iclamp((s->sample_y[first] - s->sample_y[last]) * 1000 / (int)dt,
                  -10000, 10000);
}

static void browser_fling_start(BrowserState* s, int velocity, uint32_t now) {
    s->fling_velocity = (velocity > -100 && velocity < 100) ? 0 : velocity;
    s->fling_position_q8 = s->scroll * 256;
    s->fling_last_ms = now;
}

static void browser_fling_tick(BrowserState* s, uint32_t now, int maximum) {
    uint32_t elapsed = now - s->fling_last_ms;
    int dt, old, next, decel, position;
    if (!s->fling_velocity || s->touching || s->menu_open || !elapsed) return;
    /* A long stall/resume must never teleport to a distant folder position. */
    if (elapsed > 150u) { s->fling_velocity = 0; return; }
    dt = (int)elapsed;
    old = s->fling_velocity;
    decel = imax(1, 2600 * dt / 1000);
    next = old > 0 ? imax(0, old-decel) : imin(0, old+decel);
    s->fling_position_q8 += (old+next) * dt * 128 / 1000;
    position = s->fling_position_q8 / 256;
    s->scroll = iclamp(position, 0, maximum);
    if (position <= 0 || position >= maximum) next = 0;
    s->fling_velocity = next;
    s->fling_last_ms = now;
    s->redraw = 1;
}

/* Exact 64-bit nanoseconds -> wrapped milliseconds without libgcc helpers. */
static uint32_t browser_event_ms(int64_t ns) {
    uint64_t bits = (uint64_t)ns;
    uint32_t remainder = 0, quotient = 0;
    int i;
    for (i = 63; i >= 0; --i) {
        remainder = (remainder << 1) | (uint32_t)((bits >> i) & 1u);
        quotient <<= 1;
        if (remainder >= 1000000u) { remainder -= 1000000u; quotient |= 1u; }
    }
    return quotient;
}

static void browser_draw_header_and_menu(BrowserState* s, Canvas* c, int header, int margin) {
    int bx = browser_header_button_left(c->width), scale = c->width >= 470 ? 2 : 1;
    int mw, mx, my;
    /* Paint again after cards, so scrolling cannot cover the menu button. */
    fill_rect(c, 0, 0, c->width, header, 0x1c2027);
    draw_text(c, margin, 5, "FLIPNOTE-BROWSER", scale, 0xffffff, bx-margin-4);
    draw_text(c, margin, header-13, s->directory, 1, 0x9cb3d0, bx-margin-4);
    fill_rect(c, bx+3, 3, c->width-bx-6, header-6, 0x34445a);
    draw_text(c, bx+12, 12, "MENUE", 2, 0xffffff, c->width-bx-16);
    if (!s->menu_open) return;
    mw = browser_menu_width(c->width); mx = (c->width-mw)/2;
    my = browser_menu_top(c->width);
    fill_rect(c, mx, my, mw, 184, 0x20242b);
    stroke_rect(c, mx, my, mw, 184, 2, 0x8fc7ff);
    draw_text(c, mx+10, my+12, "BROWSER-EINSTELLUNGEN", scale, 0xffffff, mw-20);
    fill_rect(c, mx+8, my+42, mw-16, 66, 0x34445a);
    draw_text(c, mx+16, my+51, "ANIMIERTE VORSCHAUEN", scale, 0xffffff, mw-32);
    draw_text(c, mx+16, my+78, s->animate_previews ? "AN - ANTIPPEN: AUSSCHALTEN" : "AUS - ANTIPPEN: EINSCHALTEN",
              1, 0x9cb3d0, mw-32);
    draw_text(c, mx+10, my+120, "AUS: NUR STATISCHE VORSCHAUBILDER", 1, 0x9cb3d0, mw-20);
    draw_text(c, mx+10, my+146, "SCHLIESSEN", scale, 0xffffff, mw-20);
}
