// SDLContainer — litehtml document_container implementation using SDL2 + FreeType
// This is the "graphics driver" between litehtml and SDL2.

#pragma once

#include <litehtml.h>
#include <SDL2/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

// RGBA pixel color helper
struct Color4 { Uint8 r, g, b, a; };

// ---- FreeType glyph cache ----
struct GlyphKey {
    uint32_t codepoint;
    int      size;
    bool operator==(const GlyphKey& o) const {
        return codepoint == o.codepoint && size == o.size;
    }
};
struct GlyphKeyHash {
    size_t operator()(const GlyphKey& k) const {
        return std::hash<uint64_t>()(((uint64_t)k.codepoint << 16) | k.size);
    }
};

struct GlyphEntry {
    SDL_Texture* tex = nullptr;
    int          w = 0, h = 0;
    int          bearing_x = 0, bearing_y = 0;
    int          advance = 0;
};

// ---- SDLContainer ----
class SDLContainer : public litehtml::document_container
{
public:
    SDLContainer(SDL_Renderer* renderer, int width, int height);
    ~SDLContainer() override;

    bool init();

    // Helper: draw text (used by browser UI)
    void draw_text(int x, int y, const char* text, int size,
                   Color4 color, bool centered);

    // Href tracking (for click detection)
    std::string last_clicked_href() const { return m_last_href; }
    void        clear_last_clicked_href()  { m_last_href.clear(); }
    void        set_base_url(const std::string& url) { m_base_url = url; }

    // ---- litehtml::document_container interface ----
    litehtml::uint_ptr create_font(const litehtml::font_description& fd,
                                   const litehtml::document* doc,
                                   litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr hFont) override;

    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override;

    void draw_text(litehtml::uint_ptr hdc, const char* text,
                   litehtml::uint_ptr hFont, litehtml::web_color color,
                   const litehtml::position& pos) override;

    litehtml::pixel_t pt_to_px(int pt) const override { return (litehtml::pixel_t)(pt * 96.0 / 72.0); }
    litehtml::pixel_t get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "sans-serif"; }

    void draw_list_marker(litehtml::uint_ptr hdc,
                          const litehtml::list_marker& marker) override;
    void load_image(const char* src, const char* baseurl,
                    bool redraw_on_ready) override;
    void get_image_size(const char* src, const char* baseurl,
                        litehtml::size& sz) override;
    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                    const std::string& url, const std::string& base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc,
                         const litehtml::background_layer& layer,
                         const litehtml::web_color& color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc,
                              const litehtml::background_layer& layer,
                              const litehtml::background_layer::linear_gradient& gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc,
                              const litehtml::background_layer& layer,
                              const litehtml::background_layer::radial_gradient& gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc,
                             const litehtml::background_layer& layer,
                             const litehtml::background_layer::conic_gradient& gradient) override;
    void draw_borders(litehtml::uint_ptr hdc,
                      const litehtml::borders& borders,
                      const litehtml::position& draw_pos,
                      bool root) override;

    void set_caption(const char* caption) override {}
    void set_base_url(const char* base_url) override {
        if (base_url) m_base_url = base_url;
    }
    void link(const std::shared_ptr<litehtml::document>& doc,
              const litehtml::element::ptr& el) override {}
    void on_anchor_click(const char* url,
                         const litehtml::element::ptr& el) override {
        if (url) m_last_href = url;
    }
    void on_mouse_event(const litehtml::element::ptr& el,
                        litehtml::mouse_event event) override {}
    void set_cursor(const char* cursor) override {}

    void transform_text(litehtml::string& text,
                        litehtml::text_transform tt) override;
    void import_css(litehtml::string& text,
                    const litehtml::string& url,
                    litehtml::string& baseurl) override {}
    void set_clip(const litehtml::position& pos,
                  const litehtml::border_radiuses& bdr_radius) override;
    void del_clip() override;
    litehtml::element::ptr create_element(const char* tag_name,
                                          const litehtml::string_map& attributes,
                                          const std::shared_ptr<litehtml::document>& doc) override {
        return nullptr;
    }
    void get_media_features(litehtml::media_features& media) const override;
    void get_language(litehtml::string& language,
                      litehtml::string& culture) const override {
        language = "zh"; culture = "CN";
    }
    litehtml::string resolve_color(const litehtml::string& color) const override {
        return {};
    }

private:
    SDL_Renderer* m_renderer;
    int           m_width, m_height;
    FT_Library    m_ft;

    struct FontFace {
        FT_Face face;
        int     size;
        bool    bold, italic;
        std::string family;
    };

    std::vector<FontFace>  m_fonts;
    std::unordered_map<GlyphKey, GlyphEntry, GlyphKeyHash> m_glyph_cache;

    // Clip stack
    std::vector<SDL_Rect> m_clip_stack;

    std::string m_base_url;
    std::string m_last_href;

    // Image cache (URLs → SDL_Texture)
    struct ImageEntry {
        SDL_Texture* tex;
        int w, h;
    };
    std::unordered_map<std::string, ImageEntry> m_image_cache;

    // Font file paths (populated in init())
    std::vector<std::string> m_font_paths;

    FT_Face load_face(const std::string& family, bool bold, bool italic);
    const GlyphEntry& get_glyph(FT_Face face, uint32_t codepoint, int size);
    int  draw_string(int x, int y, const char* utf8,
                     FT_Face face, int size, Color4 col, int max_w = -1);
    void set_sdl_clip();

    static uint32_t utf8_next(const char*& p);
};
