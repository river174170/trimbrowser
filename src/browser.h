#pragma once
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct Browser Browser;

// Complete Browser struct definition
struct Browser {
    // Rendering
    SDL_Renderer* renderer;
    int width;
    int height;

    // HTML rendering
    void* html_container;  // SDLContainer*
    void* html_doc;        // litehtml::document*
    float zoom;

    // Navigation
    std::string current_url;
    std::string current_title;
    std::vector<std::string> history;
    int history_pos;

    // UI state
    bool addressbar_visible;
    std::string addressbar_text;
    bool bookmarks_visible;
    std::vector<std::string> bookmarks;
    int bookmark_selected;
    int focused_link_index;

    // Media
    void* media_player;  // MediaPlayer*

    // Scroll
    int scroll_x;
    int scroll_y;
    int max_scroll_y;
};

// Lifecycle
Browser* browser_create(SDL_Renderer* renderer, int width, int height);
void     browser_destroy(Browser* browser);

// Navigation
void browser_navigate(Browser* browser, const char* url);
void browser_reload(Browser* browser);
bool browser_can_go_back(Browser* browser);
void browser_go_back(Browser* browser);
bool browser_can_go_forward(Browser* browser);
void browser_go_forward(Browser* browser);

// Interaction
void browser_click(Browser* browser, int x, int y);
void browser_click_focused(Browser* browser);
void browser_scroll(Browser* browser, int dx, int dy);
void browser_scroll_to_top(Browser* browser);
void browser_scroll_to_bottom(Browser* browser);
void browser_touch(Browser* browser, int x, int y, int dx, int dy, bool is_down);
void browser_text_input(Browser* browser, const char* text);
void browser_show_addressbar(Browser* browser);

// Zoom
void browser_zoom_in(Browser* browser);
void browser_zoom_out(Browser* browser);
void browser_zoom_reset(Browser* browser);

// Bookmarks
void browser_add_bookmark(Browser* browser);
void browser_remove_bookmark(Browser* browser, int index);
bool browser_has_bookmark(Browser* browser);
bool browser_show_bookmarks(Browser* browser);
void browser_close_bookmarks(Browser* browser);
void browser_select_bookmark(Browser* browser, int index);

// Playback
void browser_try_play_media(Browser* browser, const char* url);

// Per-frame
void browser_update(Browser* browser, float dt);
void browser_render(Browser* browser);

#ifdef __cplusplus
}
#endif
