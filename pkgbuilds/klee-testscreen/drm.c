/*
 * klee-testscreen, phone side: the cairo picture on the panel through DRM.
 *
 * No GPU and no libhybris on purpose: the test screen must show even when
 * those are what is broken. As for ztMETRO, the Android composer
 * (vendor.hwcomposer) and MediaTek's PQ service must first let go of
 * /dev/dri/card0 (klee-display-take, run by the service before this). Then:
 * the connected connector's preferred mode, two dumb buffers (XRGB8888, the
 * same bytes as cairo's RGB24), cairo draws straight into the back one, and
 * it is shown with a page flip. Touch comes from the touchscreen's evdev node.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/input.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <cairo.h>
#include "klee-testscreen.h"

#define MAX_TOUCH 4

struct fb {
	uint32_t handle, id, pitch;
	uint64_t size;
	uint8_t *map;
	cairo_surface_t *surf;
};

static int fb_create(int fd, int w, int h, struct fb *b)
{
	struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = 32 };
	struct drm_mode_map_dumb m = { 0 };
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0)
		return -1;
	b->handle = c.handle;
	b->pitch = c.pitch;
	b->size = c.size;
	handles[0] = c.handle;
	pitches[0] = c.pitch;
	if (drmModeAddFB2(fd, w, h, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &b->id, 0) < 0)
		return -1;
	m.handle = c.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0)
		return -1;
	b->map = mmap(NULL, c.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, m.offset);
	if (b->map == MAP_FAILED)
		return -1;
	b->surf = cairo_image_surface_create_for_data(b->map, CAIRO_FORMAT_RGB24, w, h, c.pitch);
	return 0;
}

/* the first connected connector, its preferred mode and a CRTC for it */
static int pick_output(int fd, uint32_t *conn_id, uint32_t *crtc_id, drmModeModeInfo *mode)
{
	drmModeRes *res = drmModeGetResources(fd);

	if (!res)
		return -1;
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);

		if (!c)
			continue;
		if (c->connection != DRM_MODE_CONNECTED || c->count_modes == 0) {
			drmModeFreeConnector(c);
			continue;
		}
		*mode = c->modes[0];
		for (int m = 0; m < c->count_modes; m++)
			if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) {
				*mode = c->modes[m];
				break;
			}
		*conn_id = c->connector_id;
		*crtc_id = 0;
		for (int e = 0; e < c->count_encoders && !*crtc_id; e++) {
			drmModeEncoder *enc = drmModeGetEncoder(fd, c->encoders[e]);

			if (!enc)
				continue;
			if (enc->crtc_id) {
				*crtc_id = enc->crtc_id;
			} else {
				for (int k = 0; k < res->count_crtcs; k++)
					if (enc->possible_crtcs & (1u << k)) {
						*crtc_id = res->crtcs[k];
						break;
					}
			}
			drmModeFreeEncoder(enc);
		}
		drmModeFreeConnector(c);
		drmModeFreeResources(res);
		return *crtc_id ? 0 : -1;
	}
	drmModeFreeResources(res);
	return -1;
}

/* --- touch ---------------------------------------------------------------- */

struct touchdev {
	int fd;
	int xmin, xmax, ymin, ymax;
	int x, y;
};

static int open_touch(struct touchdev *t, int max)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	int n = 0;

	if (!d)
		return 0;
	while ((e = readdir(d)) && n < max) {
		char p[64];
		unsigned long abs[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = { 0 };
		struct input_absinfo ax, ay;
		const size_t bits = 8 * sizeof(long);

		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(p, sizeof(p), "/dev/input/%s", e->d_name);
		int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs) < 0 ||
		    !(abs[ABS_MT_POSITION_X / bits] & (1UL << (ABS_MT_POSITION_X % bits))) ||
		    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) < 0 ||
		    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) < 0 ||
		    ax.maximum <= ax.minimum || ay.maximum <= ay.minimum) {
			close(fd);
			continue;
		}
		t[n] = (struct touchdev){ fd, ax.minimum, ax.maximum, ay.minimum, ay.maximum, -1, -1 };
		fprintf(stderr, "klee-testscreen: touch %s (%d..%d x %d..%d)\n", p,
			ax.minimum, ax.maximum, ay.minimum, ay.maximum);
		n++;
	}
	closedir(d);
	return n;
}

/* returns 1 when a finger went down or moved */
static int read_touch(struct touchdev *t, struct screen *sc, int W, int H)
{
	struct input_event ev[64];
	int changed = 0;
	ssize_t r;

	while ((r = read(t->fd, ev, sizeof(ev))) > 0) {
		for (size_t i = 0; i < r / sizeof(ev[0]); i++) {
			if (ev[i].type == EV_ABS && ev[i].code == ABS_MT_POSITION_X)
				t->x = ev[i].value;
			else if (ev[i].type == EV_ABS && ev[i].code == ABS_MT_POSITION_Y)
				t->y = ev[i].value;
			else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT && t->x >= 0 && t->y >= 0) {
				sc->touched = 1;
				sc->touch_x = (double)(t->x - t->xmin) * W / (t->xmax - t->xmin);
				sc->touch_y = (double)(t->y - t->ymin) * H / (t->ymax - t->ymin);
				changed = 1;
			}
		}
	}
	return changed;
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
	*(int *)data = 0;
}

int show_on_panel(struct screen *sc, void (*render)(cairo_t *, int, int, const struct screen *))
{
	int fd = -1;
	uint32_t conn, crtc;
	drmModeModeInfo mode;

	/* the composer may still be letting go of the card */
	for (int i = 0; i < 50; i++) {
		fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
		if (fd >= 0 && drmSetMaster(fd) == 0)
			break;
		if (fd >= 0)
			close(fd);
		fd = -1;
		usleep(200000);
	}
	if (fd < 0) {
		fprintf(stderr, "klee-testscreen: cannot become DRM master of /dev/dri/card0\n");
		return 1;
	}
	if (pick_output(fd, &conn, &crtc, &mode) < 0) {
		fprintf(stderr, "klee-testscreen: no connected panel\n");
		return 1;
	}
	const int W = mode.hdisplay, H = mode.vdisplay;
	fprintf(stderr, "klee-testscreen: panel %dx%d@%d (%s)\n", W, H, mode.vrefresh, mode.name);

	struct fb fb[2];
	for (int i = 0; i < 2; i++)
		if (fb_create(fd, W, H, &fb[i]) < 0) {
			fprintf(stderr, "klee-testscreen: dumb buffer: %s\n", strerror(errno));
			return 1;
		}
	/* for klee-selftest: the panel answers (the GPU is tested on its own) */
	FILE *pf = fopen("/run/klee-test/panel", "w");
	if (pf) {
		fprintf(pf, "%dx%d · %d Hz\n", W, H, mode.vrefresh);
		fclose(pf);
	}

	struct touchdev touch[MAX_TOUCH];
	int ntouch = open_touch(touch, MAX_TOUCH);
	struct pollfd pfd[MAX_TOUCH + 1];
	for (int i = 0; i < ntouch; i++)
		pfd[i] = (struct pollfd){ touch[i].fd, POLLIN, 0 };
	pfd[ntouch] = (struct pollfd){ fd, POLLIN, 0 };	/* page flip events */
	drmEventContext evctx = { .version = 2, .page_flip_handler = on_flip };

	int back = 0, shown = 0, flipping = 0, dirty = 0;
	double next = 0;
	for (;;) {

		if (now() >= next) {
			read_status(sc, STATUS_FILE);
			next = now() + 1.0;
			dirty = 1;
		}
		for (int i = 0; i < ntouch; i++)
			if (pfd[i].revents & POLLIN)
				dirty |= read_touch(&touch[i], sc, W, H);
		if (pfd[ntouch].revents & POLLIN)
			drmHandleEvent(fd, &evctx);
		/* (a change during a flip waits for the flip to finish) */
		if (dirty && !flipping) {
			dirty = 0;
			if (sc->touched)	/* (re-read so the touch row turns green now) */
				read_status(sc, STATUS_FILE);
			cairo_t *cr = cairo_create(fb[back].surf);
			render(cr, W, H, sc);
			cairo_destroy(cr);
			cairo_surface_flush(fb[back].surf);
			if (!shown) {
				if (drmModeSetCrtc(fd, crtc, fb[back].id, 0, 0, &conn, 1, &mode) < 0) {
					fprintf(stderr, "klee-testscreen: set mode: %s\n", strerror(errno));
					return 1;
				}
				shown = 1;
			} else if (drmModePageFlip(fd, crtc, fb[back].id, DRM_MODE_PAGE_FLIP_EVENT, &flipping) == 0) {
				flipping = 1;
			} else {
				drmModeSetCrtc(fd, crtc, fb[back].id, 0, 0, &conn, 1, &mode);
			}
			drmModeDirtyFB(fd, fb[back].id, NULL, 0);	/* command-mode panels */
			back ^= 1;
		}
		int ms = (int)((next - now()) * 1000);
		poll(pfd, ntouch + 1, ms > 0 ? ms : 0);
	}
}
