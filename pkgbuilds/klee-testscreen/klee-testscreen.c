/*
 * klee-testscreen: the first thing Arch shows on the POCO X8 Pro (klee).
 * A window in the middle of the screen lists what works and what does not,
 * read from /run/klee-test/status (written by klee-selftest):
 *     <ok|fail|wait|none>\t<name>\t<detail>
 * one line per item; "#title\t..." and "#foot\t..." lines set the texts
 * above and under the list. Touching the screen ticks the touch row.
 *
 * Drawing is cairo + pango into a plain ARGB image. On the phone that image
 * goes to the panel through Android's composer (HWC2 via libhybris) as a GLES
 * texture on the Mali; with --png it is written to a file instead (preview
 * on the PC, same pixels).
 *
 *   klee-testscreen                    show on the phone, refresh every second
 *   klee-testscreen --png out.png [status-file] [width height]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include "klee-testscreen.h"

static enum state parse_state(const char *s)
{
	if (!strcmp(s, "ok"))
		return ST_OK;
	if (!strcmp(s, "fail"))
		return ST_FAIL;
	if (!strcmp(s, "wait"))
		return ST_WAIT;
	return ST_NONE;
}

void read_status(struct screen *sc, const char *path)
{
	FILE *f = fopen(path, "r");
	char line[512];

	sc->n = 0;
	if (!f) {
		snprintf(sc->rows[0].name, sizeof(sc->rows[0].name), "Sistem testi");
		snprintf(sc->rows[0].detail, sizeof(sc->rows[0].detail), "başlıyor…");
		sc->rows[0].st = ST_WAIT;
		sc->n = 1;
		return;
	}
	while (fgets(line, sizeof(line), f) && sc->n < MAX_ROWS) {
		char *a, *b, *c, *save;

		line[strcspn(line, "\n")] = 0;
		a = strtok_r(line, "\t", &save);
		b = strtok_r(NULL, "\t", &save);
		c = strtok_r(NULL, "", &save);
		if (!a || !b)
			continue;
		if (!strcmp(a, "#title")) {
			snprintf(sc->title, sizeof(sc->title), "%s", b);
			continue;
		}
		if (!strcmp(a, "#foot")) {
			snprintf(sc->foot, sizeof(sc->foot), "%s", b);
			continue;
		}
		struct row *r = &sc->rows[sc->n++];
		r->st = parse_state(a);
		snprintf(r->name, sizeof(r->name), "%s", b);
		snprintf(r->detail, sizeof(r->detail), "%s", c ? c : "");
		/* the touch row is decided here, by the screen itself */
		if (!strcmp(r->name, "Dokunmatik") && sc->touched) {
			r->st = ST_OK;
			snprintf(r->detail, sizeof(r->detail), "dokunuş algılandı");
		}
	}
	fclose(f);
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
	cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
	cairo_close_path(cr);
}

/* text at (x, y) = top left, or right aligned to x when right != 0;
 * returns the height used */
static double text(cairo_t *cr, const char *s, const char *font, double size,
		   double x, double y, int right, double maxw)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *d = pango_font_description_from_string(font);
	int w, h;

	pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
	pango_layout_set_font_description(l, d);
	pango_layout_set_text(l, s, -1);
	if (maxw > 0) {
		pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
		pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
	}
	pango_layout_get_pixel_size(l, &w, &h);
	cairo_move_to(cr, right ? x - w : x, y);
	pango_cairo_show_layout(cr, l);
	pango_font_description_free(d);
	g_object_unref(l);
	return h;
}

/* text centred horizontally on the screen, wrapped to maxw; returns height */
static double ctext(cairo_t *cr, const char *s, const char *font, double size,
		    int W, double y, double maxw)
{
	PangoLayout *l = pango_cairo_create_layout(cr);
	PangoFontDescription *d = pango_font_description_from_string(font);
	int w, h;

	pango_font_description_set_absolute_size(d, size * PANGO_SCALE);
	pango_layout_set_font_description(l, d);
	pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
	pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
	pango_layout_set_text(l, s, -1);
	pango_layout_get_pixel_size(l, &w, &h);
	cairo_move_to(cr, (W - maxw) / 2.0, y);
	pango_cairo_show_layout(cr, l);
	pango_font_description_free(d);
	g_object_unref(l);
	return h;
}

static void state_color(cairo_t *cr, enum state st, double alpha)
{
	switch (st) {
	case ST_OK: cairo_set_source_rgba(cr, 0.27, 0.82, 0.52, alpha); break;
	case ST_FAIL: cairo_set_source_rgba(cr, 0.96, 0.36, 0.36, alpha); break;
	case ST_WAIT: cairo_set_source_rgba(cr, 0.98, 0.76, 0.28, alpha); break;
	default: cairo_set_source_rgba(cr, 0.55, 0.58, 0.64, alpha); break;
	}
}

/* the mark in the state dot: tick, cross, three dots, dash */
static void state_mark(cairo_t *cr, enum state st, double cx, double cy, double r)
{
	cairo_set_source_rgb(cr, 0.06, 0.07, 0.09);
	cairo_set_line_width(cr, r * 0.28);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
	switch (st) {
	case ST_OK:
		cairo_move_to(cr, cx - r * 0.45, cy + r * 0.02);
		cairo_line_to(cr, cx - r * 0.12, cy + r * 0.34);
		cairo_line_to(cr, cx + r * 0.48, cy - r * 0.32);
		cairo_stroke(cr);
		break;
	case ST_FAIL:
		cairo_move_to(cr, cx - r * 0.33, cy - r * 0.33);
		cairo_line_to(cr, cx + r * 0.33, cy + r * 0.33);
		cairo_move_to(cr, cx + r * 0.33, cy - r * 0.33);
		cairo_line_to(cr, cx - r * 0.33, cy + r * 0.33);
		cairo_stroke(cr);
		break;
	case ST_WAIT:
		for (int i = -1; i <= 1; i++) {
			cairo_arc(cr, cx + i * r * 0.42, cy, r * 0.13, 0, 2 * G_PI);
			cairo_fill(cr);
		}
		break;
	default:
		cairo_move_to(cr, cx - r * 0.35, cy);
		cairo_line_to(cr, cx + r * 0.35, cy);
		cairo_stroke(cr);
		break;
	}
}

static void draw(cairo_t *cr, int W, int H, const struct screen *sc)
{
	const double u = W / 412.0;	/* 1 design unit = 1 dp of a 412 dp wide phone */
	const char *F = "Inter";
	const char *FM = "Inter Medium";
	const char *FS = "Inter SemiBold";
	char buf[64];

	/* background: deep blue-black, a soft glow behind the window */
	cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, H);
	cairo_pattern_add_color_stop_rgb(bg, 0, 0.035, 0.043, 0.07);
	cairo_pattern_add_color_stop_rgb(bg, 1, 0.012, 0.014, 0.024);
	cairo_set_source(cr, bg);
	cairo_paint(cr);
	cairo_pattern_destroy(bg);
	cairo_pattern_t *glow = cairo_pattern_create_radial(W / 2.0, H * 0.46, 0, W / 2.0, H * 0.46, W * 0.9);
	cairo_pattern_add_color_stop_rgba(glow, 0, 0.20, 0.36, 0.95, 0.20);
	cairo_pattern_add_color_stop_rgba(glow, 1, 0.20, 0.36, 0.95, 0);
	cairo_set_source(cr, glow);
	cairo_paint(cr);
	cairo_pattern_destroy(glow);

	/* top: name of the system, then what is booting */
	cairo_set_source_rgb(cr, 0.95, 0.96, 0.98);
	ctext(cr, "klee", "Inter Bold", 34 * u, W, 64 * u, W - 40 * u);
	cairo_set_source_rgba(cr, 0.75, 0.80, 0.90, 0.75);
	ctext(cr, sc->title[0] ? sc->title : "Arch Linux · ilk açılış", F, 14 * u, W, 112 * u, W - 40 * u);

	/* the window */
	/* centred in the space between the title (ends ~140 dp) and the hint
	 * at the bottom (~90 dp) */
	const double rowh = 40 * u, head = 70 * u, pad = 20 * u;
	const double ww = W - 2 * 22 * u;
	const double wh = head + sc->n * rowh + 12 * u;
	const double top = 150 * u, bottom = H - 96 * u;
	const double wx = (W - ww) / 2, wy = top + (bottom - top - wh) / 2;

	/* shadow */
	for (int i = 6; i >= 1; i--) {
		rounded_rect(cr, wx - i * 2 * u, wy - i * 2 * u + 10 * u, ww + i * 4 * u, wh + i * 4 * u, 26 * u + i * 2 * u);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
		cairo_fill(cr);
	}
	rounded_rect(cr, wx, wy, ww, wh, 26 * u);
	cairo_pattern_t *wg = cairo_pattern_create_linear(0, wy, 0, wy + wh);
	cairo_pattern_add_color_stop_rgb(wg, 0, 0.115, 0.125, 0.16);
	cairo_pattern_add_color_stop_rgb(wg, 1, 0.085, 0.092, 0.12);
	cairo_set_source(cr, wg);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(wg);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_set_line_width(cr, 1.2 * u);
	cairo_stroke(cr);

	/* window title + summary */
	int ok = 0, fail = 0, wait = 0;
	for (int i = 0; i < sc->n; i++) {
		enum state st = sc->rows[i].st;
		ok += st == ST_OK;
		fail += st == ST_FAIL;
		wait += st == ST_WAIT;
	}
	cairo_set_source_rgb(cr, 0.95, 0.96, 0.98);
	text(cr, "Sistem testi", FS, 20 * u, wx + pad, wy + 18 * u, 0, 0);
	snprintf(buf, sizeof(buf), "%d çalışıyor · %d sorun · %d bekliyor", ok, fail, wait);
	cairo_set_source_rgba(cr, 0.75, 0.80, 0.90, 0.7);
	text(cr, buf, F, 12.5 * u, wx + pad, wy + 44 * u, 0, 0);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
	cairo_rectangle(cr, wx + pad, wy + head - 1 * u, ww - 2 * pad, 1 * u);
	cairo_fill(cr);

	/* rows */
	for (int i = 0; i < sc->n; i++) {
		const struct row *r = &sc->rows[i];
		double y = wy + head + i * rowh, cy = y + rowh / 2, dr = 10.5 * u;

		state_color(cr, r->st, 1);
		cairo_arc(cr, wx + pad + dr, cy, dr, 0, 2 * G_PI);
		cairo_fill(cr);
		state_mark(cr, r->st, wx + pad + dr, cy, dr);
		cairo_set_source_rgb(cr, 0.93, 0.94, 0.96);
		text(cr, r->name, FM, 15 * u, wx + pad + 2 * dr + 12 * u, cy - 10 * u, 0, ww * 0.42);
		state_color(cr, r->st, r->st == ST_NONE ? 0.9 : 0.95);
		if (r->st == ST_OK)
			cairo_set_source_rgba(cr, 0.75, 0.80, 0.90, 0.75);
		text(cr, r->detail, F, 13 * u, wx + ww - pad, cy - 9 * u, 1, ww * 0.44);
		if (i < sc->n - 1) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.045);
			cairo_rectangle(cr, wx + pad + 2 * dr + 12 * u, y + rowh - 0.5 * u, ww - 2 * pad - 2 * dr - 12 * u, 1 * u);
			cairo_fill(cr);
		}
	}

	/* bottom hint */
	cairo_set_source_rgba(cr, 0.75, 0.80, 0.90, 0.6);
	ctext(cr, sc->foot[0] ? sc->foot : "Dokunmatiği denemek için ekrana dokun", F, 13 * u, W, H - 84 * u, W - 60 * u);

	/* where the last touch was */
	if (sc->touch_x >= 0) {
		cairo_arc(cr, sc->touch_x, sc->touch_y, 26 * u, 0, 2 * G_PI);
		cairo_set_source_rgba(cr, 0.35, 0.55, 1.0, 0.35);
		cairo_fill(cr);
	}
}

int main(int argc, char **argv)
{
	struct screen sc = { .touch_x = -1, .touch_y = -1 };

	if (argc >= 3 && !strcmp(argv[1], "--png")) {
		int W = argc >= 6 ? atoi(argv[4]) : 1268, H = argc >= 6 ? atoi(argv[5]) : 2756;
		cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
		cairo_t *cr = cairo_create(s);

		read_status(&sc, argc >= 4 ? argv[3] : STATUS_FILE);
		draw(cr, W, H, &sc);
		cairo_surface_write_to_png(s, argv[2]);
		cairo_destroy(cr);
		cairo_surface_destroy(s);
		return 0;
	}
#ifdef KLEE_DEVICE
	return show_on_panel(&sc, draw);
#else
	fprintf(stderr, "built without the panel backend; use --png\n");
	return 1;
#endif
}
