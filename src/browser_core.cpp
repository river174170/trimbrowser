// browser_core.cpp — HTTP fetching + HTML parsing + litehtml integration
// All rendering done via SDL2 + freetype, no X11/Wayland required

#include "browser.h"
#include "litehtml_container.h"

#include <litehtml.h>
#include <SDL2/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <string>
#include <vector>
#include <deque>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

#include <curl/curl.h>   // bundled libcurl

// -------------------------------------------------------------------------
// HTTP fetch (via libcurl)
// -------------------------------------------------------------------------
struct FetchResult {
    std::string body;
    std::string content_type;
    long status_code = 0;
    std::string final_url;
    bool success = false;
    std::string error;
};

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

static FetchResult fetch_url(const std::string& url)
{
    FetchResult res;
    CURL* curl = curl_easy_init();
    if (!curl) {
        res.error = "curl_easy_init failed";
        return res;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "TrimBrowser/1.0 (TrimUI Smart Pro; aarch64 Linux)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);   // TrimUI has no CA bundle
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        res.success = true;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status_code);
        char* eff_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
        if (eff_url) res.final_url = eff_url;
        struct curl_header* hdr = nullptr;
        if (curl_easy_header(curl, "Content-Type", 0, CURLH_HEADER, -1, &hdr) == CURLHE_OK && hdr)
            res.content_type = hdr->value;
    } else {
        res.error = curl_easy_strerror(code);
    }

    curl_easy_cleanup(curl);
    return res;
}

// -------------------------------------------------------------------------
// Browser state
// -------------------------------------------------------------------------
struct Browser {
    // SDL
    SDL_Renderer*  renderer;
    int            win_w, win_h;

    // litehtml
    SDLContainer*           container;
    litehtml::document::ptr doc;
    std::string             current_url;
    std::string             current_html;
    int                     scroll_x = 0, scroll_y = 0;
    int                     doc_height = 0;
    float                   zoom = 1.0f;

    // Navigation history
    std::deque<std::string> history_back;
    std::deque<std::string> history_fwd;

    // Loading state
    enum State { IDLE, LOADING, RENDERING, ERROR_STATE } state = IDLE;
    std::string  status_text;
    std::string  error_text;
    float        load_spin = 0.0f;    // spinner angle

    // Background fetch thread
    std::thread       fetch_thread;
    std::mutex        fetch_mutex;
    FetchResult       fetch_result;
    std::atomic<bool> fetch_done{false};

    // Address bar
    bool        addressbar_visible = false;
    std::string addressbar_text;
    int         addressbar_cursor = 0;

    // Touch tracking
    int  touch_start_x = 0, touch_start_y = 0;
    int  touch_last_x = 0,  touch_last_y = 0;
    bool touching = false;

    // Bookmarks
    struct Bookmark {
        std::string url;
        std::string title;
        std::string saved_at;
    };
    std::vector<Bookmark> bookmarks;
    std::string bookmarks_file;
    bool bookmarks_visible = false;
    int bookmark_selected = 0;

    // Playback state
    bool is_playing = false;
    std::string media_url;
};

// -------------------------------------------------------------------------
// Forward declarations
// -------------------------------------------------------------------------
static void start_fetch(Browser* b, const std::string& url);
static void build_doc(Browser* b);
static void render_loading(Browser* b);
static void render_addressbar(Browser* b);
static void addressbar_confirm(Browser* b);

// -------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------
Browser* browser_create(SDL_Renderer* renderer, int width, int height)
{
    curl_global_init(CURL_GLOBAL_ALL);

    Browser* b = new Browser();
    b->renderer = renderer;
    b->win_w    = width;
    b->win_h    = height;

    b->container = new SDLContainer(renderer, width, height);
    if (!b->container->init()) {
        delete b->container;
        delete b;
        curl_global_cleanup();
        return nullptr;
    }

    // Load bookmarks on startup
    load_bookmarks(b);

    return b;
}

void browser_destroy(Browser* b)
{
    if (!b) return;
    if (b->fetch_thread.joinable()) b->fetch_thread.join();
    b->doc.reset();
    delete b->container;
    delete b;
    curl_global_cleanup();
}

// -------------------------------------------------------------------------
// Navigation
// -------------------------------------------------------------------------
static std::string resolve_url(const std::string& base, const std::string& rel)
{
    // Very basic resolver - litehtml handles most relative URLs internally,
    // but we need this for top-level navigation
    if (rel.empty()) return base;
    if (rel.substr(0,4) == "http") return rel;
    if (rel[0] == '/') {
        // scheme://host + rel
        size_t p = base.find("://");
        if (p == std::string::npos) return rel;
        size_t q = base.find('/', p + 3);
        std::string origin = (q == std::string::npos) ? base : base.substr(0, q);
        return origin + rel;
    }
    // relative to current dir
    size_t p = base.rfind('/');
    if (p == std::string::npos) return rel;
    return base.substr(0, p + 1) + rel;
}

static void push_history(Browser* b)
{
    if (!b->current_url.empty())
        b->history_back.push_back(b->current_url);
    b->history_fwd.clear();
}

void browser_navigate(Browser* b, const char* url)
{
    if (!b || !url || !*url) return;
    push_history(b);
    start_fetch(b, url);
}

void browser_reload(Browser* b)
{
    if (!b || b->current_url.empty()) return;
    start_fetch(b, b->current_url);
}

bool browser_can_go_back(Browser* b)
{
    return b && !b->history_back.empty();
}

void browser_go_back(Browser* b)
{
    if (!browser_can_go_back(b)) return;
    b->history_fwd.push_front(b->current_url);
    std::string url = b->history_back.back();
    b->history_back.pop_back();
    start_fetch(b, url);
}

bool browser_can_go_forward(Browser* b)
{
    return b && !b->history_fwd.empty();
}

void browser_go_forward(Browser* b)
{
    if (!browser_can_go_forward(b)) return;
    b->history_back.push_back(b->current_url);
    std::string url = b->history_fwd.front();
    b->history_fwd.pop_front();
    start_fetch(b, url);
}

// -------------------------------------------------------------------------
// Async fetch
// -------------------------------------------------------------------------
static void start_fetch(Browser* b, const std::string& url)
{
    if (b->fetch_thread.joinable()) b->fetch_thread.join();

    b->state = Browser::LOADING;
    b->status_text = "Loading: " + url;
    b->fetch_done  = false;
    b->scroll_x    = 0;
    b->scroll_y    = 0;

    std::string fetch_url_str = url;
    // Auto-add https:// if no scheme
    if (fetch_url_str.find("://") == std::string::npos)
        fetch_url_str = "https://" + fetch_url_str;

    b->fetch_thread = std::thread([b, fetch_url_str](){
        FetchResult res = fetch_url(fetch_url_str);
        {
            std::lock_guard<std::mutex> lg(b->fetch_mutex);
            b->fetch_result = std::move(res);
        }
        b->fetch_done = true;
    });
}

// -------------------------------------------------------------------------
// Build litehtml document from HTML string
// -------------------------------------------------------------------------
static void build_doc(Browser* b)
{
    b->container->set_base_url(b->current_url);

    b->doc = litehtml::document::createFromString(
        b->current_html.c_str(),
        b->container,
        nullptr   // no master CSS override; we supply one in container
    );

    if (b->doc) {
        b->doc->render(b->win_w - 16);  // 16px margin for scrollbar
        b->doc_height = b->doc->height();
        b->state = Browser::IDLE;
        b->status_text = b->current_url;
    } else {
        b->state = Browser::ERROR_STATE;
        b->error_text = "Failed to parse HTML";
    }
}

// -------------------------------------------------------------------------
// Interaction
// -------------------------------------------------------------------------
void browser_click(Browser* b, int x, int y)
{
    if (!b) return;
    if (b->addressbar_visible) {
        // Click outside address bar = dismiss
        if (y > 60) {
            b->addressbar_visible = false;
        }
        return;
    }
    if (!b->doc) return;

    // Convert screen coords to document coords
    int dx = x + b->scroll_x;
    int dy = y + b->scroll_y;

    litehtml::position::vector redraw;
    b->doc->on_lbutton_down(dx, dy, dx, dy, redraw);
    b->doc->on_lbutton_up  (dx, dy, dx, dy, redraw);

    // Check if a link was clicked
    std::string href = b->container->last_clicked_href();
    if (!href.empty()) {
        std::string full = resolve_url(b->current_url, href);
        b->container->clear_last_clicked_href();

        // Try to play media if it's a media URL
        if (is_media_url(full)) {
            browser_try_play_media(b, full.c_str());
        } else {
            browser_navigate(b, full.c_str());
        }
    }
}

void browser_click_focused(Browser* b)
{
    if (!b || !b->doc) return;
    // Let litehtml handle pressing Enter on focused element
    litehtml::position::vector redraw;
    b->doc->on_lbutton_down(0, 0, 0, 0, redraw);
}

void browser_scroll(Browser* b, int dx, int dy)
{
    if (!b) return;
    b->scroll_x = std::max(0, b->scroll_x + dx);
    b->scroll_y = std::max(0, std::min(b->doc_height - b->win_h, b->scroll_y + dy));
}

void browser_scroll_to_top(Browser* b)
{
    if (b) { b->scroll_x = 0; b->scroll_y = 0; }
}

void browser_scroll_to_bottom(Browser* b)
{
    if (b) b->scroll_y = std::max(0, b->doc_height - b->win_h);
}

void browser_touch(Browser* b, int x, int y, int dx, int dy, bool is_down)
{
    if (!b) return;
    if (is_down) {
        b->touch_start_x = x; b->touch_start_y = y;
        b->touch_last_x  = x; b->touch_last_y  = y;
        b->touching      = true;
    } else if (b->touching) {
        browser_scroll(b, -dx, -dy);
        b->touch_last_x = x;
        b->touch_last_y = y;
    }
}

void browser_text_input(Browser* b, const char* text)
{
    if (!b) return;
    if (b->addressbar_visible) {
        b->addressbar_text.insert(b->addressbar_cursor, text);
        b->addressbar_cursor += (int)strlen(text);
    }
}

void browser_show_addressbar(Browser* b)
{
    if (!b) return;
    b->addressbar_visible = true;
    b->addressbar_text    = b->current_url;
    b->addressbar_cursor  = (int)b->addressbar_text.size();
    SDL_StartTextInput();
}

void browser_zoom_in(Browser* b)
{
    if (!b) return;
    b->zoom = std::min(3.0f, b->zoom + 0.1f);
    if (b->doc) { b->doc->render((int)(b->win_w / b->zoom)); b->doc_height = b->doc->height(); }
}

void browser_zoom_out(Browser* b)
{
    if (!b) return;
    b->zoom = std::max(0.3f, b->zoom - 0.1f);
    if (b->doc) { b->doc->render((int)(b->win_w / b->zoom)); b->doc_height = b->doc->height(); }
}

void browser_zoom_reset(Browser* b)
{
    if (!b) return;
    b->zoom = 1.0f;
    if (b->doc) { b->doc->render(b->win_w); b->doc_height = b->doc->height(); }
}

// -------------------------------------------------------------------------
// Per-frame update
// -------------------------------------------------------------------------
void browser_update(Browser* b, float dt)
{
    if (!b) return;

    // Check if fetch thread finished
    if (b->state == Browser::LOADING && b->fetch_done) {
        b->fetch_done = false;
        FetchResult res;
        {
            std::lock_guard<std::mutex> lg(b->fetch_mutex);
            res = std::move(b->fetch_result);
        }
        if (b->fetch_thread.joinable()) b->fetch_thread.join();

        if (res.success) {
            b->current_url  = res.final_url.empty() ? b->current_url : res.final_url;
            b->current_html = std::move(res.body);
            b->state        = Browser::RENDERING;
            b->status_text  = "Rendering...";
            // Build doc synchronously (fast enough for reasonable pages)
            build_doc(b);
        } else {
            b->state      = Browser::ERROR_STATE;
            b->error_text = "Network error: " + res.error;
        }
    }

    // Animate spinner
    if (b->state == Browser::LOADING) {
        b->load_spin += dt * 360.0f;
        if (b->load_spin >= 360.0f) b->load_spin -= 360.0f;
    }
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------
static void draw_filled_rect(SDL_Renderer* r, int x, int y, int w, int h,
                             Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_SetRenderDrawBlendMode(r, A < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, R, G, B, A);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void render_loading(Browser* b)
{
    // Dark overlay
    draw_filled_rect(b->renderer, 0, 0, b->win_w, b->win_h, 20, 20, 20, 240);

    // Simple spinning ring (8 dots)
    int cx = b->win_w / 2, cy = b->win_h / 2;
    const int R = 30, dot_r = 5;
    for (int i = 0; i < 8; i++) {
        float angle = (b->load_spin + i * 45.0f) * 3.14159f / 180.0f;
        int x = cx + (int)(R * cosf(angle));
        int y = cy + (int)(R * sinf(angle));
        Uint8 alpha = (Uint8)(255 * (i + 1) / 8);
        draw_filled_rect(b->renderer, x - dot_r, y - dot_r,
                         dot_r * 2, dot_r * 2, 100, 180, 255, alpha);
    }

    // Status text via container font
    b->container->draw_text(cx, cy + 60, b->status_text.c_str(), 16,
                            {180, 180, 180, 255}, true /* centered */);
}

static void render_error(Browser* b)
{
    draw_filled_rect(b->renderer, 0, 0, b->win_w, b->win_h, 25, 10, 10, 255);
    b->container->draw_text(b->win_w/2, b->win_h/2 - 20,
                            "Failed to load page", 20, {255, 80, 80, 255}, true);
    b->container->draw_text(b->win_w/2, b->win_h/2 + 20,
                            b->error_text.c_str(), 14, {200, 200, 200, 255}, true);
    b->container->draw_text(b->win_w/2, b->win_h/2 + 60,
                            "Press B to go back, Y to enter URL", 14,
                            {150, 150, 150, 255}, true);
}

static void render_toolbar(Browser* b)
{
    // Top toolbar background
    draw_filled_rect(b->renderer, 0, 0, b->win_w, 44, 35, 35, 40, 230);

    // Back button indicator
    if (browser_can_go_back(b)) {
        b->container->draw_text(12, 12, "<", 18, {200, 200, 255, 255}, false);
    }

    // URL (truncated)
    std::string disp = b->current_url;
    if (disp.size() > 80) disp = disp.substr(0, 77) + "...";
    b->container->draw_text(40, 12, disp.c_str(), 14, {180, 200, 255, 255}, false);

    // Controls hint (right side)
    b->container->draw_text(b->win_w - 8, 12,
        "[Y] URL  [SELECT] Bookmarks  [X] Reload  [START] Quit",
        12, {120, 120, 140, 200}, true /* right-align via centered=false is fine */);

    // Scrollbar
    if (b->doc_height > b->win_h) {
        int bar_h = b->win_h - 44;
        int thumb_h = std::max(20, bar_h * b->win_h / b->doc_height);
        int thumb_y = 44 + (bar_h - thumb_h) * b->scroll_y /
                           std::max(1, b->doc_height - b->win_h);
        draw_filled_rect(b->renderer, b->win_w - 6, 44, 6, bar_h, 50, 50, 50, 180);
        draw_filled_rect(b->renderer, b->win_w - 6, thumb_y, 6, thumb_h,
                         100, 150, 255, 200);
    }
}

static void render_addressbar(Browser* b)
{
    // Semi-transparent overlay
    draw_filled_rect(b->renderer, 0, 0, b->win_w, 70, 20, 20, 25, 230);
    // Input box
    draw_filled_rect(b->renderer, 8, 8, b->win_w - 16, 52, 40, 40, 50, 255);
    // Border
    SDL_SetRenderDrawColor(b->renderer, 80, 130, 255, 255);
    SDL_Rect box{8, 8, b->win_w - 16, 52};
    SDL_RenderDrawRect(b->renderer, &box);

    // Text
    b->container->draw_text(20, 20, b->addressbar_text.c_str(),
                            16, {230, 230, 255, 255}, false);
    // Cursor
    SDL_SetRenderDrawColor(b->renderer, 200, 200, 255, 255);
    // rough cursor position
    SDL_Rect cur{20 + b->addressbar_cursor * 9, 18, 2, 28};
    SDL_RenderFillRect(b->renderer, &cur);
}

void browser_render(Browser* b)
{
    if (!b) return;

    switch (b->state) {
    case Browser::LOADING:
        render_loading(b);
        return;
    case Browser::ERROR_STATE:
        render_error(b);
        render_toolbar(b);
        if (b->addressbar_visible) render_addressbar(b);
        return;
    case Browser::IDLE:
    case Browser::RENDERING:
        break;
    }

    if (!b->doc) {
        draw_filled_rect(b->renderer, 0, 0, b->win_w, b->win_h, 20, 20, 20, 255);
        b->container->draw_text(b->win_w/2, b->win_h/2,
            "Press Y to enter a URL", 16, {180, 180, 180, 255}, true);
        render_toolbar(b);
        if (b->addressbar_visible) render_addressbar(b);
        return;
    }

    // Render litehtml document
    // Apply zoom via SDL render scale
    SDL_RenderSetScale(b->renderer, b->zoom, b->zoom);

    litehtml::position clip;
    clip.x = 0; clip.y = 44;   // leave toolbar space
    clip.width  = b->win_w;
    clip.height = b->win_h - 44;

    b->doc->draw(reinterpret_cast<litehtml::uint_ptr>(b->renderer),
                 -b->scroll_x, 44 - b->scroll_y,
                 &clip);

    SDL_RenderSetScale(b->renderer, 1.0f, 1.0f);

    render_toolbar(b);
    if (b->addressbar_visible) render_addressbar(b);
    if (b->bookmarks_visible) render_bookmarks(b);
}

// -------------------------------------------------------------------------
// Bookmarks - file I/O
// -------------------------------------------------------------------------
static std::string get_home_dir()
{
    const char* home = getenv("HOME");
    if (home) return std::string(home);
    return "/mnt/extsd";  // TrimUI SD card mount point
}

static void load_bookmarks(Browser* b)
{
    b->bookmarks_file = get_home_dir() + "/.trimbrowser/bookmarks.json";
    FILE* f = fopen(b->bookmarks_file.c_str(), "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return; }

    std::string json(len + 1, 0);
    fread(&json[0], 1, len, f);
    fclose(f);

    // Simple JSON parse: [{"url":"...","title":"...",...}]
    size_t pos = 0;
    while ((pos = json.find("{\"url\":", pos)) != std::string::npos) {
        Browser::Bookmark bm;
        size_t url_start = json.find("\"", pos + 6) + 1;
        size_t url_end = json.find("\"", url_start);
        if (url_end == std::string::npos) break;
        bm.url = json.substr(url_start, url_end - url_start);

        size_t title_start = json.find("\"title\":", url_end);
        if (title_start != std::string::npos) {
            title_start = json.find("\"", title_start + 8) + 1;
            title_end = json.find("\"", title_start);
            if (title_end != std::string::npos)
                bm.title = json.substr(title_start, title_end - title_start);
        }
        if (bm.title.empty()) bm.title = bm.url;
        b->bookmarks.push_back(bm);
        pos = url_end;
    }
}

static void save_bookmarks(Browser* b)
{
    if (b->bookmarks.empty()) {
        unlink(b->bookmarks_file.c_str());
        return;
    }

    // Create directory if needed
    std::string dir = get_home_dir() + "/.trimbrowser";
    mkdir(dir.c_str(), 0755);

    FILE* f = fopen(b->bookmarks_file.c_str(), "w");
    if (!f) return;

    fputs("[", f);
    for (size_t i = 0; i < b->bookmarks.size(); i++) {
        const auto& bm = b->bookmarks[i];
        fprintf(f, "{\"url\":\"%s\",\"title\":\"%s\"}",
                bm.url.c_str(), bm.title.c_str());
        if (i + 1 < b->bookmarks.size()) fputs(",", f);
    }
    fputs("]", f);
    fclose(f);
}

// -------------------------------------------------------------------------
// Bookmarks - API
// -------------------------------------------------------------------------
void browser_add_bookmark(Browser* b)
{
    if (!b || b->current_url.empty()) return;

    // Check if already exists
    for (const auto& bm : b->bookmarks) {
        if (bm.url == b->current_url) return;
    }

    Browser::Bookmark bm;
    bm.url = b->current_url;
    // Extract title from URL if no doc title
    if (b->doc && !b->doc->title().empty()) {
        bm.title = b->doc->title();
    } else {
        // Generate title from URL
        size_t pos = b->current_url.find("://");
        std::string host = (pos != std::string::npos) ?
            b->current_url.substr(pos + 3) : b->current_url;
        size_t slash = host.find('/');
        if (slash != std::string::npos) host = host.substr(0, slash);
        bm.title = host;
    }

    b->bookmarks.push_back(bm);
    save_bookmarks(b);
    b->status_text = "Bookmark added: " + bm.title;
}

void browser_remove_bookmark(Browser* b, int index)
{
    if (!b || index < 0 || (size_t)index >= b->bookmarks.size()) return;
    b->bookmarks.erase(b->bookmarks.begin() + index);
    save_bookmarks(b);
    if (b->bookmark_selected >= (int)b->bookmarks.size())
        b->bookmark_selected = std::max(0, (int)b->bookmarks.size() - 1);
}

bool browser_has_bookmark(Browser* b)
{
    if (!b || b->current_url.empty()) return false;
    for (const auto& bm : b->bookmarks) {
        if (bm.url == b->current_url) return true;
    }
    return false;
}

bool browser_show_bookmarks(Browser* b)
{
    if (!b) return false;
    if (b->bookmarks.empty()) {
        b->bookmarks_visible = true;
        return true;
    }
    b->bookmarks_visible = true;
    b->bookmark_selected = 0;
    return true;
}

void browser_close_bookmarks(Browser* b)
{
    if (b) b->bookmarks_visible = false;
}

void browser_select_bookmark(Browser* b, int index)
{
    if (!b || index < 0 || (size_t)index >= b->bookmarks.size()) return;
    b->bookmarks_visible = false;
    browser_navigate(b, b->bookmarks[index].url.c_str());
}

// -------------------------------------------------------------------------
// Playback
// -------------------------------------------------------------------------
static bool is_media_url(const std::string& url)
{
    static const char* exts[] = {
        ".mp4", ".webm", ".mkv", ".avi", ".mov",
        ".mp3", ".ogg", ".wav", ".flac", ".aac",
        ".m3u8", ".mpd", nullptr
    };
    for (int i = 0; exts[i]; i++) {
        size_t pos = url.rfind(exts[i]);
        if (pos != std::string::npos && pos + strlen(exts[i]) == url.size())
            return true;
    }
    // Also check for common streaming patterns
    if (url.find("m3u8") != std::string::npos) return true;
    if (url.find("mpd") != std::string::npos) return true;
    return false;
}

void browser_try_play_media(Browser* b, const char* url)
{
    if (!b || !url || !*url) return;
    if (!is_media_url(url)) return;

    b->media_url = url;
    b->is_playing = true;
    b->status_text = "Playing: " + std::string(url);

    // Launch player - try gst-play first (GStreamer on TrimUI)
    // If that fails, try ffplay
    std::string cmd = "gst-play-1.0 \"" + b->media_url + "\" &";
    int ret = system(cmd.c_str());
    if (ret != 0) {
        cmd = "ffplay -fs \"" + b->media_url + "\" &";
        system(cmd.c_str());
    }
}

// -------------------------------------------------------------------------
// Render bookmarks overlay
// -------------------------------------------------------------------------
static void render_bookmarks(Browser* b)
{
    if (!b->bookmarks_visible) return;

    // Dark overlay
    draw_filled_rect(b->renderer, 0, 0, b->win_w, b->win_h, 15, 15, 20, 240);

    // Title bar
    draw_filled_rect(b->renderer, 0, 0, b->win_w, 50, 30, 30, 45, 255);
    b->container->draw_text(20, 15, "Bookmarks", 20, {255, 255, 255, 255}, false);
    b->container->draw_text(b->win_w - 20, 15, "[SELECT] Open  [A] Add  [B] Close", 12, {180, 180, 180, 255}, true);

    if (b->bookmarks.empty()) {
        b->container->draw_text(b->win_w/2, b->win_h/2,
            "No bookmarks yet. Press A to add current page.", 16, {160, 160, 160, 255}, true);
        return;
    }

    // List items
    int y = 60;
    int line_h = 36;
    int visible = (b->win_h - 60) / line_h;
    int start = std::max(0, b->bookmark_selected - visible / 2);

    for (int i = start; i < (int)b->bookmarks.size() && y < b->win_h - 20; i++) {
        bool selected = (i == b->bookmark_selected);
        int x = 20;

        if (selected) {
            draw_filled_rect(b->renderer, x - 8, y - 4, b->win_w - 40, line_h,
                           50, 80, 140, 180);
        }

        // Index number
        char idx[8];
        snprintf(idx, sizeof(idx), "%d.", i + 1);
        b->container->draw_text(x, y + 2, idx, 14,
                               selected ? {255, 200, 100, 255} : {140, 140, 160, 255}, false);
        x += 30;

        // Title
        std::string title = b->bookmarks[i].title;
        if (title.size() > 50) title = title.substr(0, 47) + "...";
        b->container->draw_text(x, y + 2, title.c_str(), 14,
                               selected ? {255, 255, 200, 255} : {220, 220, 220, 255}, false);
        y += line_h;
    }
}
