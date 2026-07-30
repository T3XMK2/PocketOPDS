#include <inkview.h>

extern "C" {
#include "config.h"
#include "net.h"
#include "opds.h"
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

enum Screen { SERVERS, SERVER_ACTIONS, CATALOG, DETAIL, ERROR_SCREEN };
enum Icon { BACK_ICON, ADD_ICON, EDIT_ICON, DELETE_ICON, SEARCH_ICON };
enum EditStep { EDIT_NONE, EDIT_NAME, EDIT_URL, EDIT_USER, EDIT_PASS };

const int MARGIN = 16;
const int RAIL = 6;
const int ACTION = 64;
const int GAP = 8;
const int ROW = 88;

struct CatalogPage {
    int server;
    std::string url;
    std::string title;
    opds_feed_t *feed;
    CatalogPage() : server(0), feed(NULL) {}
};

class App {
public:
    App() : screen(SERVERS), width(0), height(0), headerH(92), offset(0),
            selectedServer(-1), selectedEntry(-1), editStep(EDIT_NONE),
            body(NULL), bold(NULL), small(NULL), titleFont(NULL), loading(false),
            confirmDelete(false), keyboardOpen(false) {
        name[0] = url[0] = user[0] = pass[0] = 0;
        searchText[0] = 0;
    }

    ~App() { clearCatalogs(); }

    int event(int type, int p1, int p2) {
        if (type == EVT_INIT) {
            SetPanelType(0);
            net_init();
            config_load();
            return 1;
        }
        if (type == EVT_SHOW) {
            SetPanelType(0);
            layout();
            if (!keyboardOpen) draw();
            return 1;
        }
        if (type == EVT_REPAINT) {
            /* InkView sends repaint events while its system keyboard is being
             * created and dismissed. Redrawing our framebuffer here makes the
             * keyboard disappear while it still owns input focus. */
            if (!keyboardOpen) draw();
            return 1;
        }
        if (type == EVT_POINTERUP) {
            tap(p1, p2);
            return 1;
        }
        if (type == EVT_KEYDOWN) {
            if (p1 == KEY_BACK) goBack();
            else if (p1 == KEY_UP || p1 == KEY_LEFT) scroll(-1);
            else if (p1 == KEY_DOWN || p1 == KEY_RIGHT) scroll(1);
            return 1;
        }
        if (type == EVT_EXIT) {
            clearCatalogs();
            if (body) CloseFont(body);
            if (bold) CloseFont(bold);
            if (small) CloseFont(small);
            if (titleFont) CloseFont(titleFont);
            body = bold = small = titleFont = NULL;
            net_cleanup();
            return 1;
        }
        return 0;
    }

    void keyboardDone(char *text) {
        keyboardOpen = false;
        if (!text) {
            editStep = EDIT_NONE;
            SetHardTimer("poredraw", redrawTimer, 120);
            return;
        }
        char *target = editStep == EDIT_NAME ? name :
                       editStep == EDIT_URL ? url :
                       editStep == EDIT_USER ? user : pass;
        size_t size = editStep == EDIT_NAME ? sizeof(name) :
                      editStep == EDIT_URL ? sizeof(url) :
                      editStep == EDIT_USER ? sizeof(user) : sizeof(pass);
        std::strncpy(target, text, size - 1);
        target[size - 1] = 0;
        if (editStep == EDIT_NAME) scheduleField(EDIT_URL);
        else if (editStep == EDIT_URL) scheduleField(EDIT_USER);
        else if (editStep == EDIT_USER) scheduleField(EDIT_PASS);
        else {
            if (url[0] && !std::strstr(url, "://")) {
                char fixed[MAX_URL_LEN];
                std::snprintf(fixed, sizeof(fixed), "http://%s", url);
                std::strncpy(url, fixed, sizeof(url) - 1);
            }
            if (selectedServer >= 0)
                config_update_server(selectedServer, name, url, user, pass);
            else
                config_add_server(name, url, user, pass);
            editStep = EDIT_NONE;
            selectedServer = -1;
            SetHardTimer("poredraw", redrawTimer, 120);
        }
    }

    void loadCatalog() {
        if (pages.empty()) return;
        CatalogPage &page = pages.back();
        loading = true;
        draw();
        if (!net_wifi_ensure(30)) {
            showError("Network unavailable",
                      "PocketOPDS could not connect to Wi-Fi.");
            return;
        }
        opds_feed_t *feed = opds_fetch(page.url.c_str(),
                                      g_servers[page.server].username,
                                      g_servers[page.server].password);
        if (!feed || feed->error[0]) {
            std::string message = feed ? feed->error : "Out of memory";
            if (feed) opds_feed_free(feed);
            showError("Catalog error", message);
            return;
        }
        char resolved[OPDS_MAX_URL];
        for (int i = 0; i < feed->count; ++i) {
            opds_entry_t &entry = feed->entries[i];
            if (entry.nav_url[0]) {
                opds_resolve_url(feed, entry.nav_url, resolved, sizeof(resolved));
                std::strncpy(entry.nav_url, resolved, sizeof(entry.nav_url) - 1);
            }
            if (entry.download_url[0]) {
                opds_resolve_url(feed, entry.download_url,
                                 resolved, sizeof(resolved));
                std::strncpy(entry.download_url, resolved,
                             sizeof(entry.download_url) - 1);
            }
            if (entry.alt_url[0]) {
                opds_resolve_url(feed, entry.alt_url, resolved, sizeof(resolved));
                std::strncpy(entry.alt_url, resolved, sizeof(entry.alt_url) - 1);
            }
        }
        if (feed->next_page_url[0]) {
            opds_resolve_url(feed, feed->next_page_url,
                             resolved, sizeof(resolved));
            std::strncpy(feed->next_page_url, resolved,
                         sizeof(feed->next_page_url) - 1);
        }
        if (feed->search_url[0]) {
            opds_resolve_url(feed, feed->search_url,
                             resolved, sizeof(resolved));
            std::strncpy(feed->search_url, resolved,
                         sizeof(feed->search_url) - 1);
        }
        page.feed = feed;
        loading = false;
        offset = 0;
        screen = CATALOG;
        draw();
    }

    void download() {
        if (pages.empty() || selectedEntry < 0) return;
        opds_entry_t &entry = pages.back().feed->entries[selectedEntry];
        const char *mime = entry.download_mime;
        const char *ext = std::strstr(mime, "pdf") ? ".pdf" :
                          std::strstr(mime, "mobi") ? ".mobi" :
                          std::strstr(mime, "fb2") ? ".fb2" : ".epub";
        char safe[220];
        size_t j = 0;
        for (size_t i = 0; entry.title[i] && j < sizeof(safe) - 1; ++i) {
            char c = entry.title[i];
            safe[j++] = std::strchr("\\/:*?\"<>|", c) ? '_' : c;
        }
        safe[j] = 0;
        iv_mkdir(FLASHDIR "/Books", 0755);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/Books/%s%s",
                      FLASHDIR, safe[0] ? safe : "book", ext);
        loading = true;
        draw();
        char error[256] = {0};
        char disposition[512] = {0};
        int code = net_download_to_file(entry.download_url,
                                        g_servers[pages.back().server].username,
                                        g_servers[pages.back().server].password,
                                        path, disposition, sizeof(disposition),
                                        error, sizeof(error));
        loading = false;
        if (code < 200 || code >= 300 || error[0]) {
            unlink(path);
            showError("Download failed", error[0] ? error : "HTTP error");
            return;
        }
        iv_sync();
        SendEventTo(OTHERTASKS, EVT_STARTSCAN, 0, 0);
        Message(ICON_INFORMATION, "Download complete", path, 2500);
        draw();
    }

    void searchDone(char *text) {
        keyboardOpen = false;
        if (!text || !text[0]) {
            SetHardTimer("poredraw", redrawTimer, 120);
            return;
        }
        std::strncpy(searchText, text, sizeof(searchText) - 1);
        searchText[sizeof(searchText) - 1] = 0;
        SetHardTimer("posearch", searchTimer, 150);
    }

    void performSearch() {
        if (pages.empty() || !pages.back().feed) {
            draw();
            return;
        }
        CatalogPage &page = pages.back();
        std::string templ = page.feed->search_url;

        /* Some feeds point to an OpenSearch description instead of exposing
         * the {searchTerms} template directly. */
        if (templ.find("{searchTerms") == std::string::npos) {
            net_response_t *description =
                net_get(templ.c_str(), g_servers[page.server].username,
                         g_servers[page.server].password);
            if (description && description->data) {
                const char *attr = std::strstr(description->data, "template=");
                if (attr) {
                    attr += 9;
                    char quote = *attr++;
                    const char *end = (quote == '"' || quote == '\'')
                                      ? std::strchr(attr, quote) : NULL;
                    if (end) templ.assign(attr, end - attr);
                }
            }
            net_response_free(description);
        }

        std::string encoded;
        const char hex[] = "0123456789ABCDEF";
        for (const unsigned char *p =
                 reinterpret_cast<const unsigned char *>(searchText); *p; ++p) {
            unsigned char c = *p;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == '.' || c == '~') encoded += static_cast<char>(c);
            else {
                encoded += '%';
                encoded += hex[c >> 4];
                encoded += hex[c & 15];
            }
        }
        size_t marker = templ.find("{searchTerms}");
        size_t markerLength = 13;
        if (marker == std::string::npos) {
            marker = templ.find("{searchTerms?}");
            markerLength = 14;
        }
        if (marker != std::string::npos)
            templ.replace(marker, markerLength, encoded);
        else
            templ += (templ.find('?') == std::string::npos ? "?query=" : "&query=")
                     + encoded;

        int server = page.server;
        std::string title = std::string("Search: ") + searchText;
        openCatalog(server, templ.c_str(), title.c_str());
    }

private:
    Screen screen;
    int width, height, headerH;
    int offset;
    int selectedServer;
    int selectedEntry;
    EditStep editStep;
    ifont *body, *bold, *small, *titleFont;
    bool loading;
    bool confirmDelete;
    bool keyboardOpen;
    std::string errorTitle, errorText;
    Screen errorReturn;
    std::vector<CatalogPage> pages;
    char name[MAX_NAME_LEN], url[MAX_URL_LEN];
    char user[MAX_CRED_LEN], pass[MAX_CRED_LEN];
    char searchText[OPDS_MAX_TITLE];

    static int rightX(int position) {
        return ScreenWidth() - MARGIN - ACTION - position * (ACTION + GAP);
    }

    void layout() {
        int w = ScreenWidth(), h = ScreenHeight();
        if (w < 400 || h < 600) return;
        if (w == width && h == height && body) return;
        if (body) CloseFont(body);
        if (bold) CloseFont(bold);
        if (small) CloseFont(small);
        if (titleFont) CloseFont(titleFont);
        width = w; height = h;
        int fs = std::max(22, std::min(42, height / 52));
        body = OpenFont(DEFAULTFONT, fs, 1);
        bold = OpenFont(DEFAULTFONTB, fs, 1);
        small = OpenFont(DEFAULTFONT, std::max(18, fs - 7), 1);
        titleFont = OpenFont(DEFAULTFONTB, fs + 8, 1);
        headerH = fs * 2 + 28;
    }

    void iconButton(int x, Icon icon, bool filled = false) {
        int y = 14, cx = x + ACTION / 2, cy = y + ACTION / 2;
        if (filled) FillArea(x, y, ACTION, ACTION, BLACK);
        else DrawRect(x, y, ACTION, ACTION, BLACK);
        int c = filled ? WHITE : BLACK;
        if (icon == BACK_ICON) {
            DrawLine(cx + 12, cy - 14, cx - 10, cy, c);
            DrawLine(cx - 10, cy, cx + 12, cy + 14, c);
            DrawLine(cx - 9, cy, cx + 16, cy, c);
        } else if (icon == ADD_ICON) {
            DrawLine(cx - 14, cy, cx + 14, cy, c);
            DrawLine(cx, cy - 14, cx, cy + 14, c);
        } else if (icon == EDIT_ICON) {
            DrawLine(cx - 13, cy + 12, cx + 10, cy - 11, c);
            DrawLine(cx - 8, cy + 15, cx + 15, cy - 8, c);
        } else if (icon == DELETE_ICON) {
            DrawRect(cx - 11, cy - 8, 22, 23, c);
            DrawLine(cx - 15, cy - 13, cx + 15, cy - 13, c);
        } else {
            DrawRect(cx - 13, cy - 13, 22, 22, c);
            DrawLine(cx + 7, cy + 7, cx + 16, cy + 16, c);
        }
    }

    void header(const char *title, const char *subtitle) {
        FillArea(0, 14, RAIL, headerH - 28, BLACK);
        int left = screen == SERVERS ? 22 : 96;
        SetFont(titleFont, BLACK);
        DrawString(left, 15, title);
        SetFont(small, BLACK);
        if (subtitle && subtitle[0]) DrawString(left + 2, headerH - 29, subtitle);
        DrawLine(0, headerH - 1, width, headerH - 1, BLACK);
        if (screen != SERVERS) iconButton(MARGIN, BACK_ICON);
        if (screen == SERVERS) iconButton(rightX(0), ADD_ICON);
        else if (screen == SERVER_ACTIONS) {
            iconButton(rightX(1), EDIT_ICON);
            iconButton(rightX(0), DELETE_ICON, confirmDelete);
        } else if (screen == CATALOG && !pages.empty() && pages.back().feed &&
                   pages.back().feed->search_url[0]) {
            iconButton(rightX(0), SEARCH_ICON);
        }
    }

    void rowBase(int y, bool navigation) {
        FillArea(MARGIN, y + 28, navigation ? RAIL : 8,
                 navigation ? 32 : 8, BLACK);
        DrawLine(MARGIN, y + ROW - 1, width - MARGIN, y + ROW - 1, BLACK);
    }

    void draw() {
        if (!body || width == 0) return;
        ClearScreen();
        if (screen == SERVERS) drawServers();
        else if (screen == SERVER_ACTIONS) drawServerActions();
        else if (screen == CATALOG) drawCatalog();
        else if (screen == DETAIL) drawDetail();
        else drawError();
        FullUpdate();
    }

    void drawServers() {
        header("PocketOPDS", "Libraries and OPDS servers");
        int visible = std::max(1, (height - headerH) / ROW);
        for (int r = 0; r < visible && offset + r < g_server_count; ++r) {
            int i = offset + r, y = headerH + r * ROW;
            rowBase(y, true);
            SetFont(bold, BLACK);
            DrawTextRect(34, y + 13, width - 130, 38,
                         g_servers[i].name, ALIGN_LEFT | DOTS);
            SetFont(small, BLACK);
            DrawTextRect(34, y + 51, width - 130, 30,
                         g_servers[i].url, ALIGN_LEFT | DOTS);
            int bx = width - MARGIN - 56;
            DrawRect(bx, y + 20, 56, 56, BLACK);
            int cx = bx + 28, cy = y + 48;
            DrawLine(cx - 11, cy + 10, cx + 9, cy - 10, BLACK);
            DrawLine(cx - 6, cy + 13, cx + 14, cy - 7, BLACK);
        }
        if (!g_server_count) {
            SetFont(body, BLACK);
            DrawTextRect(28, headerH + 100, width - 56, 160,
                         "No servers configured.\nTap + to add one.",
                         ALIGN_CENTER | VALIGN_MIDDLE);
        }
    }

    void drawServerActions() {
        if (selectedServer < 0 || selectedServer >= g_server_count) {
            screen = SERVERS;
            drawServers();
            return;
        }
        server_t &server = g_servers[selectedServer];
        header(server.name, confirmDelete
               ? "Tap the highlighted delete button again"
               : "Server settings");
        int y = headerH + 28;
        SetFont(small, BLACK);
        DrawString(24, y, "ADDRESS");
        SetFont(body, BLACK);
        DrawTextRect(24, y + 34, width - 48, 100,
                     server.url, ALIGN_LEFT | VALIGN_TOP);
        DrawLine(MARGIN, y + 145, width - MARGIN, y + 145, BLACK);
        SetFont(small, BLACK);
        DrawString(24, y + 174, "AUTHENTICATION");
        SetFont(body, BLACK);
        DrawString(24, y + 212,
                   server.username[0] ? server.username : "Anonymous access");
        SetFont(small, BLACK);
        DrawTextRect(24, y + 285, width - 48, 90,
                     "Use the pencil button to edit this server. "
                     "The trash button requires a second tap.",
                     ALIGN_LEFT | VALIGN_TOP);
    }

    void drawCatalog() {
        const CatalogPage &page = pages.back();
        header(page.title.c_str(), loading ? "Connecting..." : "Tap a book to view it");
        if (loading || !page.feed) {
            SetFont(body, BLACK);
            DrawTextRect(30, headerH + 100, width - 60, 150,
                         "Loading catalog...", ALIGN_CENTER | VALIGN_MIDDLE);
            return;
        }
        int total = page.feed->count + (page.feed->next_page_url[0] ? 1 : 0);
        int visible = std::max(1, (height - headerH) / ROW);
        for (int r = 0; r < visible && offset + r < total; ++r) {
            int i = offset + r, y = headerH + r * ROW;
            if (i == page.feed->count) {
                rowBase(y, true);
                SetFont(bold, BLACK);
                DrawTextRect(34, y, width - 68, ROW,
                             "Load more", ALIGN_LEFT | VALIGN_MIDDLE);
                continue;
            }
            const opds_entry_t &e = page.feed->entries[i];
            rowBase(y, e.type == ENTRY_NAVIGATION);
            SetFont(bold, BLACK);
            DrawTextRect(34, y + 12, width - 68, 40,
                         e.title, ALIGN_LEFT | DOTS);
            SetFont(small, BLACK);
            const char *sub = e.type == ENTRY_NAVIGATION ? "Open section" :
                              e.author[0] ? e.author : opds_mime_short(e.download_mime);
            DrawTextRect(34, y + 51, width - 68, 30,
                         sub, ALIGN_LEFT | DOTS);
        }
    }

    void drawDetail() {
        opds_entry_t &e = pages.back().feed->entries[selectedEntry];
        header(e.title, loading ? "Downloading..." : "Book details");
        SetFont(bold, BLACK);
        DrawTextRect(24, headerH + 22, width - 48, 52,
                     e.author[0] ? e.author : "Unknown author",
                     ALIGN_LEFT | DOTS);
        SetFont(body, BLACK);
        DrawTextRect(24, headerH + 88, width - 48,
                     height - headerH - 190,
                     e.summary[0] ? e.summary : "No description available.",
                     ALIGN_LEFT | VALIGN_TOP);
        int y = height - 86;
        FillArea(MARGIN, y, width - MARGIN * 2, 58, BLACK);
        SetFont(body, WHITE);
        DrawTextRect(MARGIN, y, width - MARGIN * 2, 58,
                     loading ? "Downloading" : "Download",
                     ALIGN_CENTER | VALIGN_MIDDLE);
    }

    void drawError() {
        header(errorTitle.c_str(), "The operation did not complete");
        SetFont(body, BLACK);
        DrawTextRect(24, headerH + 35, width - 48,
                     height - headerH - 60, errorText.c_str(),
                     ALIGN_LEFT | VALIGN_TOP);
    }

    void tap(int x, int y) {
        if (y < headerH) {
            if (screen != SERVERS && x < 88) goBack();
            else if (screen == SERVERS && x >= rightX(0)) beginEdit(-1);
            else if (screen == SERVER_ACTIONS && x >= rightX(0)) {
                if (!confirmDelete) {
                    confirmDelete = true;
                    draw();
                } else {
                    config_remove_server(selectedServer);
                    selectedServer = -1;
                    confirmDelete = false;
                    offset = 0;
                    screen = SERVERS;
                    draw();
                }
            } else if (screen == SERVER_ACTIONS && x >= rightX(1)) {
                beginEdit(selectedServer);
            } else if (screen == CATALOG && x >= rightX(0) &&
                       !pages.empty() && pages.back().feed &&
                       pages.back().feed->search_url[0]) {
                searchText[0] = 0;
                keyboardOpen = true;
                OpenKeyboard("Search catalog", searchText,
                             sizeof(searchText) - 1, KBD_NORMAL,
                             searchCallback);
            }
            return;
        }
        if (screen == SERVERS) {
            int i = offset + (y - headerH) / ROW;
            if (i < 0 || i >= g_server_count) return;
            if (x >= width - 90) {
                selectedServer = i;
                confirmDelete = false;
                screen = SERVER_ACTIONS;
                draw();
            }
            else openCatalog(i, g_servers[i].url, g_servers[i].name);
        } else if (screen == CATALOG && !loading && !pages.empty()) {
            int i = offset + (y - headerH) / ROW;
            if (i == pages.back().feed->count &&
                pages.back().feed->next_page_url[0]) {
                openCatalog(pages.back().server,
                            pages.back().feed->next_page_url, "More books");
                return;
            }
            if (i < 0 || i >= pages.back().feed->count) return;
            opds_entry_t &e = pages.back().feed->entries[i];
            if (e.type == ENTRY_NAVIGATION)
                openCatalog(pages.back().server, e.nav_url, e.title);
            else {
                selectedEntry = i;
                screen = DETAIL;
                draw();
            }
        } else if (screen == DETAIL && y >= height - 105 && !loading) {
            SetHardTimer("podownload", downloadTimer, 50);
        }
    }

    void scroll(int direction) {
        int count = screen == SERVERS ? g_server_count :
                    screen == CATALOG && !pages.empty() && pages.back().feed
                    ? pages.back().feed->count +
                      (pages.back().feed->next_page_url[0] ? 1 : 0) : 0;
        int visible = std::max(1, (height - headerH) / ROW);
        if (direction > 0 && offset + visible < count) ++offset;
        else if (direction < 0 && offset > 0) --offset;
        draw();
    }

    void goBack() {
        if (screen == SERVERS) { CloseApp(); return; }
        if (screen == SERVER_ACTIONS) {
            selectedServer = -1;
            confirmDelete = false;
            screen = SERVERS;
            draw();
            return;
        }
        if (screen == ERROR_SCREEN) {
            if (errorReturn == CATALOG && !pages.empty() &&
                pages.back().feed == NULL) {
                pages.pop_back();
                offset = 0;
                screen = pages.empty() ? SERVERS : CATALOG;
                draw();
                return;
            }
            screen = errorReturn;
            draw();
            return;
        }
        if (screen == DETAIL) {
            screen = CATALOG;
            selectedEntry = -1;
            draw();
            return;
        }
        if (screen == CATALOG) {
            if (!pages.empty()) {
                if (pages.back().feed) opds_feed_free(pages.back().feed);
                pages.pop_back();
            }
            offset = 0;
            screen = pages.empty() ? SERVERS : CATALOG;
            draw();
        }
    }

    void openCatalog(int server, const char *target, const char *title) {
        CatalogPage page;
        page.server = server;
        page.url = target ? target : "";
        page.title = title && title[0] ? title : "Catalog";
        pages.push_back(page);
        offset = 0;
        screen = CATALOG;
        loading = true;
        draw();
        SetHardTimer("poload", loadTimer, 50);
    }

    void beginEdit(int index) {
        selectedServer = index;
        if (index >= 0) {
            std::strncpy(name, g_servers[index].name, sizeof(name) - 1);
            std::strncpy(url, g_servers[index].url, sizeof(url) - 1);
            std::strncpy(user, g_servers[index].username, sizeof(user) - 1);
            std::strncpy(pass, g_servers[index].password, sizeof(pass) - 1);
        } else name[0] = url[0] = user[0] = pass[0] = 0;
        scheduleField(EDIT_NAME);
    }

    void scheduleField(EditStep step) {
        editStep = step;
        /* Never create a new InkView keyboard inside the callback that closes
         * the previous one. B300 firmware needs one event-loop turn first. */
        SetHardTimer("pofield", fieldTimer, 150);
    }

    void showField() {
        if (editStep == EDIT_NONE) return;
        const char *label = editStep == EDIT_NAME ? "Server name" :
                            editStep == EDIT_URL ? "Server URL" :
                            editStep == EDIT_USER ? "Username (optional)" :
                                                    "Password (optional)";
        char *value = editStep == EDIT_NAME ? name :
                      editStep == EDIT_URL ? url :
                      editStep == EDIT_USER ? user : pass;
        int maximum = editStep == EDIT_URL ? MAX_URL_LEN - 1 :
                      editStep == EDIT_NAME ? MAX_NAME_LEN - 1 :
                                              MAX_CRED_LEN - 1;
        keyboardOpen = true;
        OpenKeyboard(label, value, maximum,
                     editStep == EDIT_PASS ? KBD_PASSWORD : KBD_NORMAL,
                     keyboardCallback);
    }

    void showError(const std::string &title, const std::string &text) {
        loading = false;
        errorReturn = pages.empty() ? SERVERS : CATALOG;
        errorTitle = title;
        errorText = text;
        screen = ERROR_SCREEN;
        draw();
    }

    void clearCatalogs() {
        for (size_t i = 0; i < pages.size(); ++i)
            if (pages[i].feed) opds_feed_free(pages[i].feed);
        pages.clear();
    }

public:
    static App *instance;
    static void keyboardCallback(char *text) {
        if (instance) instance->keyboardDone(text);
    }
    static void loadTimer() {
        if (instance) instance->loadCatalog();
    }
    static void downloadTimer() {
        if (instance) instance->download();
    }
    static void searchCallback(char *text) {
        if (instance) instance->searchDone(text);
    }
    static void searchTimer() {
        if (instance) instance->performSearch();
    }
    static void fieldTimer() {
        if (instance) instance->showField();
    }
    static void redrawTimer() {
        if (instance) instance->draw();
    }
};

App *App::instance = NULL;
App app;

void keyboardCallback(char *text) { App::keyboardCallback(text); }
void loadTimer() { App::loadTimer(); }
void downloadTimer() { App::downloadTimer(); }

int handler(int type, int p1, int p2) { return app.event(type, p1, p2); }

} // namespace

int main(int, char **) {
    App::instance = &app;
    InkViewMain(handler);
    return 0;
}
