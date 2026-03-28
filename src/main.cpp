// TrimBrowser - SDL2 web browser for TrimUI Smart Pro
// Depends only on: SDL2, freetype, libssl/crypto 1.1, libz (all system-provided)
// HTTP fetching: libcurl (bundled) or raw openssl socket

#include "browser.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <csignal>

// Screen dimensions for TrimUI Smart Pro
#define SCREEN_W 1280
#define SCREEN_H 720
#define FPS      60

static bool g_running = true;

void signal_handler(int) { g_running = false; }

int main(int argc, char* argv[])
{
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    // ---------- SDL2 init ----------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Try fullscreen first (TrimUI always runs fullscreen)
    SDL_Window* window = SDL_CreateWindow(
        "TrimBrowser",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );
    if (!window) {
        // Fall back to windowed
        window = SDL_CreateWindow("TrimBrowser",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    }
    if (!window) {
        fprintf(stderr, "CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Get actual window size (may differ from requested in fullscreen)
    int win_w, win_h;
    SDL_GetWindowSize(window, &win_w, &win_h);

    // ---------- Browser core init ----------
    Browser* browser = browser_create(renderer, win_w, win_h);
    if (!browser) {
        fprintf(stderr, "browser_create failed\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Load initial URL from argv or default to homepage
    const char* start_url = (argc > 1) ? argv[1] : "https://www.baidu.com";
    browser_navigate(browser, start_url);

    // Open joystick/gamepad (TrimUI controller)
    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) break;
        }
    }

    // ---------- Main loop ----------
    const int frame_delay = 1000 / FPS;
    Uint32 last_frame = SDL_GetTicks();

    // For analog scroll acceleration
    float scroll_vel_y = 0.0f;

    while (g_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {

            case SDL_QUIT:
                g_running = false;
                break;

            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                // Navigation
                case SDLK_ESCAPE:
                case SDLK_q:
                    // Q or ESC = quit or close bookmarks
                    if (browser->bookmarks_visible) {
                        browser_close_bookmarks(browser);
                    } else if (browser->addressbar_visible) {
                        browser_show_addressbar(browser); // toggle off
                    } else if (browser_can_go_back(browser)) {
                        browser_go_back(browser);
                    } else {
                        g_running = false;
                    }
                    break;
                case SDLK_TAB:
                    // TAB = toggle bookmarks
                    if (browser->bookmarks_visible) {
                        browser_close_bookmarks(browser);
                    } else {
                        browser_show_bookmarks(browser);
                    }
                    break;
                case SDLK_BACKSPACE:
                    if (browser_can_go_back(browser))
                        browser_go_back(browser);
                    break;
                case SDLK_r:
                    browser_reload(browser);
                    break;

                // Scrolling
                case SDLK_UP:
                    browser_scroll(browser, 0, -80);
                    break;
                case SDLK_DOWN:
                    browser_scroll(browser, 0, 80);
                    break;
                case SDLK_LEFT:
                    browser_scroll(browser, -80, 0);
                    break;
                case SDLK_RIGHT:
                    browser_scroll(browser, 80, 0);
                    break;
                case SDLK_PAGEUP:
                    browser_scroll(browser, 0, -win_h);
                    break;
                case SDLK_PAGEDOWN:
                    browser_scroll(browser, 0, win_h);
                    break;
                case SDLK_HOME:
                    browser_scroll_to_top(browser);
                    break;
                case SDLK_END:
                    browser_scroll_to_bottom(browser);
                    break;

                // Bookmark navigation (when bookmarks visible)
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (browser->bookmarks_visible) {
                        browser_select_bookmark(browser, browser->bookmark_selected);
                    } else if (browser->addressbar_visible) {
                        // Confirm URL in address bar
                        browser_navigate(browser, browser->addressbar_text.c_str());
                        browser_show_addressbar(browser); // toggle off
                    }
                    break;
                case SDLK_UP:
                    if (browser->bookmarks_visible) {
                        browser->bookmark_selected = std::max(0, browser->bookmark_selected - 1);
                    } else {
                        browser_scroll(browser, 0, -80);
                    }
                    break;
                case SDLK_DOWN:
                    if (browser->bookmarks_visible) {
                        browser->bookmark_selected = std::min((int)browser->bookmarks.size() - 1, browser->bookmark_selected + 1);
                    } else {
                        browser_scroll(browser, 0, 80);
                    }
                    break;

                // URL bar toggle
                case SDLK_l:
                case SDLK_F6:
                    browser_show_addressbar(browser);
                    break;

                // Zoom
                case SDLK_EQUALS:
                case SDLK_PLUS:
                    browser_zoom_in(browser);
                    break;
                case SDLK_MINUS:
                    browser_zoom_out(browser);
                    break;
                case SDLK_0:
                    browser_zoom_reset(browser);
                    break;

                default: break;
                }
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_B:       // B = back
                    if (browser_can_go_back(browser))
                        browser_go_back(browser);
                    break;
                case SDL_CONTROLLER_BUTTON_X:       // X = reload
                    browser_reload(browser);
                    break;
                case SDL_CONTROLLER_BUTTON_Y:       // Y = address bar
                    browser_show_addressbar(browser);
                    break;
                case SDL_CONTROLLER_BUTTON_START:   // START = quit
                    g_running = false;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    browser_scroll(browser, 0, -60);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    browser_scroll(browser, 0, 60);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    browser_scroll(browser, -60, 0);
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    browser_scroll(browser, 60, 0);
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:   // L = page up
                    browser_scroll(browser, 0, -win_h + 60);
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:  // R = page down
                    browser_scroll(browser, 0, win_h - 60);
                    break;
                case SDL_CONTROLLER_BUTTON_A:       // A = click focused link or add bookmark
                    if (browser->bookmarks_visible) {
                        browser_add_bookmark(browser);
                    } else {
                        browser_click_focused(browser);
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_SELECT:  // SELECT = toggle bookmarks
                    if (browser->bookmarks_visible) {
                        browser_close_bookmarks(browser);
                    } else {
                        browser_show_bookmarks(browser);
                    }
                    break;
                case SDL_CONTROLLER_BUTTON_GUIDE:   // Guide/Home = add bookmark
                    browser_add_bookmark(browser);
                    break;
                default: break;
                }
                break;

            case SDL_CONTROLLERAXISMOTION: {
                // Right analog stick = scroll
                const int DEAD = 8000;
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                    int val = ev.caxis.value;
                    if (val > DEAD || val < -DEAD) {
                        scroll_vel_y = (float)val / 32767.0f * 12.0f;
                    } else {
                        scroll_vel_y = 0.0f;
                    }
                }
                break;
            }

            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    browser_click(browser, ev.button.x, ev.button.y);
                }
                break;

            case SDL_MOUSEWHEEL:
                browser_scroll(browser, ev.wheel.x * -30, ev.wheel.y * -60);
                break;

            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
                // Touch: map to screen coordinates
                browser_touch(browser,
                    (int)(ev.tfinger.x * win_w),
                    (int)(ev.tfinger.y * win_h),
                    (int)(ev.tfinger.dx * win_w),
                    (int)(ev.tfinger.dy * win_h),
                    ev.type == SDL_FINGERDOWN);
                break;

            case SDL_TEXTINPUT:
                browser_text_input(browser, ev.text.text);
                break;

            default: break;
            }
        }

        // Apply analog scroll velocity
        if (scroll_vel_y != 0.0f) {
            browser_scroll(browser, 0, (int)scroll_vel_y);
        }

        // Update + render
        Uint32 now = SDL_GetTicks();
        float dt = (now - last_frame) / 1000.0f;
        last_frame = now;

        browser_update(browser, dt);

        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        browser_render(browser);

        SDL_RenderPresent(renderer);

        // Cap framerate
        Uint32 elapsed = SDL_GetTicks() - now;
        if (elapsed < (Uint32)frame_delay)
            SDL_Delay(frame_delay - elapsed);
    }

    // ---------- Cleanup ----------
    if (controller) SDL_GameControllerClose(controller);
    browser_destroy(browser);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
