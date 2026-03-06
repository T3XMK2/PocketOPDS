#include <inkview.h>
#include "app.h"
#include "config.h"
#include "net.h"

/* ── Shared global UI state ─────────────────────────────────────────────────
 * Defined here; declared extern in app.h
 * ─────────────────────────────────────────────────────────────────────────── */

int    g_screen_w  = 0;
int    g_screen_h  = 0;
int    g_item_h    = 0;
int    g_font_size = 0;

static int g_ui_shown = 0;

ifont *g_font_body  = NULL;
ifont *g_font_bold  = NULL;
ifont *g_font_small = NULL;

/* ── Compute layout parameters from actual screen size ─────────────────────
 * PocketBook Verse Pro: 1264 × 1680 px @ 300 dpi (6" e-ink)
 * We derive all sizes from screen height to be resolution-independent.     */

static void init_layout(void)
{
    g_screen_w = ScreenWidth();
    g_screen_h = ScreenHeight();

    /* Scale font size: roughly 28pt on a 300-dpi screen */
    g_font_size = g_screen_h / 48;
    if (g_font_size < 20) g_font_size = 20;
    if (g_font_size > 60) g_font_size = 60;

    /* List row height: two text lines + padding — compact text-list feel */
    g_item_h = g_font_size * 2 + 24;
    if (g_item_h < 60) g_item_h = 60;

    /* Open fonts (anti-aliased, 1 = AA on) */
    g_font_body  = OpenFont(DEFAULTFONT,  g_font_size,     1);
    g_font_bold  = OpenFont(DEFAULTFONTB, g_font_size,     1);
    g_font_small = OpenFont(DEFAULTFONT,  g_font_size - 6, 1);
}

static void cleanup_layout(void)
{
    if (g_font_body)  { CloseFont(g_font_body);  g_font_body  = NULL; }
    if (g_font_bold)  { CloseFont(g_font_bold);  g_font_bold  = NULL; }
    if (g_font_small) { CloseFont(g_font_small); g_font_small = NULL; }
}

/* ── Main event handler ─────────────────────────────────────────────────────
 * InkView calls this for system-level events.  Most UI work is done inside
 * show_server_list() (called from EVT_SHOW) which blocks via OpenList().   */

static int app_handler(int type, int par1, int par2)
{
    switch (type) {

        /* ── Startup ── */
        case EVT_INIT:
            net_init();
            config_load();
            return 1;

        /* ── First show: initialise layout and open server list ── */
        case EVT_SHOW:
            if (!g_ui_shown) {
                g_ui_shown = 1;
                init_layout();   /* screen is ready here – ScreenWidth()/Height() valid */
                show_server_list();
                /* OpenList is event-driven (non-blocking) – do NOT call
                 * CloseApp() here.  CloseApp() is called from the LIST_EXIT
                 * handler in screen_servers.c when the user presses Back. */
            }
            return 1;

        /* ── Repaint: just flush – do NOT restart the UI ── */
        case EVT_REPAINT:
            FullUpdate();
            return 1;

        /* ── Hardware buttons ── */
        case EVT_KEYDOWN:
            /* KEY_HOME quits at any depth; KEY_BACK is handled by each
             * list's LIST_EXIT so it does not propagate here. */
            if (par1 == KEY_HOME) {
                CloseApp();
                return 1;
            }
            return 0;

        /* ── Shutdown ── */
        case EVT_EXIT:
            cleanup_layout();
            net_cleanup();
            return 1;

        default:
            break;
    }
    return 0;
}

/* ── Program entry point ────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    InkViewMain(app_handler);
    return 0;
}
