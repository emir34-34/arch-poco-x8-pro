/* klee-testscreen: what the drawing (klee-testscreen.c) and the panel
 * backend (panel.c) share */
#include <cairo.h>

#define STATUS_FILE "/run/klee-test/status"
#define MAX_ROWS 24

enum state { ST_OK, ST_FAIL, ST_WAIT, ST_NONE };

struct row {
	enum state st;
	char name[64];
	char detail[96];
};

struct screen {
	char title[96];
	char foot[160];
	struct row rows[MAX_ROWS];
	int n;
	int touched;		/* the touch row turns green once touched */
	double touch_x, touch_y;	/* last touch, -1 = none */
};

void read_status(struct screen *sc, const char *path);
int show_on_panel(struct screen *sc, void (*render)(cairo_t *, int, int, const struct screen *));
