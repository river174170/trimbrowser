// SDLContainer implementation — bridges litehtml to SDL2 + FreeType
#include "litehtml_container.h"

#include <litehtml.h>
#include <SDL2/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <fstream>

// ---- Helpers ----
static inline void sdl_color(SDL_Renderer* r, litehtml::web_color c, Uint8 a = 255) {
    SDL_SetRenderDrawColor(r, c.red, c.green, c.blue, (Uint8)((int)c.alpha * a / 255));
}

// ---- Constructor / Destructor ----
SDLContainer::SDLContainer(SDL_Renderer* renderer, int width, int height)
    : m_renderer(renderer), m_width(width), m_height(height), m_ft(nullptr)
{
}

SDLContainer::~SDLContainer()
{
    for (auto& fe : m_fonts) FT_Done_Face(fe.face);
    for (auto& [k, g] : m_glyph_cache) if (g.tex) SDL_DestroyTexture(g.tex);
    for (auto& [k, img] : m_image_cache) if (img.tex) SDL_DestroyTexture(img.tex);
    if (m_ft) FT_Done_FreeType(m_ft);
}

bool SDLContainer::init()
{
    if (FT_Init_FreeType(&m_ft)) {
        fprintf(stderr, "FT_Init_FreeType failed\n");
        return false;
    }

    // Collect system font paths
    // TrimUI Smart Pro typically has fonts in /usr/share/fonts or /mnt/SDCARD/Fonts
    static const char* search_dirs[] = {
        "/usr/share/fonts",
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/opentype",
        "/mnt/SDCARD/Fonts",
        "/mnt/SDCARD/.system/res",
        "./fonts",   // beside the binary
        nullptr
    };

    for (int i = 0; search_dirs[i]; i++) {
        // Simple glob for .ttf/.otf (we walk manually to avoid glob dependency)
        // On TrimUI we just try common names
    }

    // Try to find at least one workable font
    static const char* font_tries[] = {
        // TrimUI / Allwinner system fonts
        "/usr/share/fonts/droid/DroidSans.ttf",
        "/usr/share/fonts/DroidSans.ttf",
        "/usr/share/fonts/truetype/droid/DroidSans.ttf",
        "/usr/share/fonts/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        // Bundled font (we ship one in ./fonts/)
        "./fonts/NotoSans-Regular.ttf",
        "./fonts/DroidSans.ttf",
        "./fonts/font.ttf",
        nullptr
    };

    for (int i = 0; font_tries[i]; i++) {
        FT_Face face;
        if (FT_New_Face(m_ft, font_tries[i], 0, &face) == 0) {
            m_font_paths.push_back(font_tries[i]);
            FT_Done_Face(face);
            break;
        }
    }

    if (m_font_paths.empty()) {
        fprintf(stderr, "Warning: no font found, text rendering will fail\n");
        // Still return true — images/backgrounds will work
    }
    return true;
}

// ---- UTF-8 decoder ----
uint32_t SDLContainer::utf8_next(const char*& p)
{
    unsigned char c = (unsigned char)*p++;
    if      (c < 0x80) return c;
    else if ((c & 0xE0) == 0xC0) {
        uint32_t r = (c & 0x1F) << 6;
        r |= ((unsigned char)*p++ & 0x3F);
        return r;
    } else if ((c & 0xF0) == 0xE0) {
        uint32_t r = (c & 0x0F) << 12;
        r |= ((unsigned char)*p++ & 0x3F) << 6;
        r |= ((unsigned char)*p++ & 0x3F);
        return r;
    } else if ((c & 0xF8) == 0xF0) {
        uint32_t r = (c & 0x07) << 18;
        r |= ((unsigned char)*p++ & 0x3F) << 12;
        r |= ((unsigned char)*p++ & 0x3F) << 6;
        r |= ((unsigned char)*p++ & 0x3F);
        return r;
    }
    return '?';
}

// ---- FreeType face loading ----
FT_Face SDLContainer::load_face(const std::string& family, bool bold, bool italic)
{
    if (m_font_paths.empty()) return nullptr;
    // For now use first available font
    FT_Face face;
    if (FT_New_Face(m_ft, m_font_paths[0].c_str(), 0, &face)) return nullptr;
    return face;
}

// ---- Glyph cache ----
const GlyphEntry& SDLContainer::get_glyph(FT_Face face, uint32_t cp, int size)
{
    GlyphKey key{cp, size};
    auto it = m_glyph_cache.find(key);
    if (it != m_glyph_cache.end()) return it->second;

    GlyphEntry& ge = m_glyph_cache[key];

    if (!face) return ge;

    FT_Set_Pixel_Sizes(face, 0, size);
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) return ge;

    FT_GlyphSlot g = face->glyph;
    if (g->bitmap.width == 0 || g->bitmap.rows == 0) {
        ge.advance = g->advance.x >> 6;
        return ge;
    }

    // Create SDL texture from FT bitmap
    SDL_Surface* surf = SDL_CreateRGBSurface(0, g->bitmap.width, g->bitmap.rows, 32,
                                              0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surf) return ge;

    for (unsigned row = 0; row < g->bitmap.rows; row++) {
        for (unsigned col = 0; col < g->bitmap.width; col++) {
            Uint8 alpha = g->bitmap.buffer[row * g->bitmap.pitch + col];
            Uint32* px = (Uint32*)((Uint8*)surf->pixels + row * surf->pitch + col * 4);
            *px = SDL_MapRGBA(surf->format, 255, 255, 255, alpha);
        }
    }

    ge.tex       = SDL_CreateTextureFromSurface(m_renderer, surf);
    ge.w         = g->bitmap.width;
    ge.h         = g->bitmap.rows;
    ge.bearing_x = g->bitmap_left;
    ge.bearing_y = g->bitmap_top;
    ge.advance   = g->advance.x >> 6;

    SDL_FreeSurface(surf);
    if (ge.tex) SDL_SetTextureBlendMode(ge.tex, SDL_BLENDMODE_BLEND);
    return ge;
}

// ---- Draw UTF-8 string ----
int SDLContainer::draw_string(int x, int y, const char* utf8,
                               FT_Face face, int size, Color4 col, int max_w)
{
    if (!utf8 || !*utf8 || !face) return 0;
    FT_Set_Pixel_Sizes(face, 0, size);

    int pen_x = x;
    const char* p = utf8;
    while (*p) {
        uint32_t cp = utf8_next(p);
        const GlyphEntry& ge = get_glyph(face, cp, size);
        if (ge.tex) {
            SDL_SetTextureColorMod(ge.tex, col.r, col.g, col.b);
            SDL_SetTextureAlphaMod(ge.tex, col.a);
            SDL_Rect dst{pen_x + ge.bearing_x,
                         y - ge.bearing_y,
                         ge.w, ge.h};
            SDL_RenderCopy(m_renderer, ge.tex, nullptr, &dst);
        }
        pen_x += ge.advance;
        if (max_w > 0 && pen_x - x >= max_w) break;
    }
    return pen_x - x;
}

// ---- Public draw_text helper ----
void SDLContainer::draw_text(int x, int y, const char* text, int size,
                              Color4 color, bool centered)
{
    if (!text || m_font_paths.empty()) return;
    FT_Face face;
    if (FT_New_Face(m_ft, m_font_paths[0].c_str(), 0, &face)) return;

    if (centered) {
        // Measure width first
        int w = text_width(text, reinterpret_cast<litehtml::uint_ptr>(face));
        x -= w / 2;
    }
    draw_string(x, y + size, text, face, size, color);
    FT_Done_Face(face);
}

// ===========================================================================
// litehtml::document_container interface
// ===========================================================================

struct FontHandle {
    FT_Face face;
    int     size;
    bool    bold, italic, underline, strikeout;
};

litehtml::uint_ptr SDLContainer::create_font(const litehtml::font_description& fd,
                                              const litehtml::document* doc,
                                              litehtml::font_metrics* fm)
{
    int size = fd.size;
    if (size <= 0) size = 16;

    FT_Face face = load_face(fd.family, fd.weight >= 600, fd.style == litehtml::font_style_italic);

    if (fm && face) {
        FT_Set_Pixel_Sizes(face, 0, size);
        fm->height     = (int)(face->size->metrics.height >> 6);
        fm->ascent     = (int)(face->size->metrics.ascender >> 6);
        fm->descent    = -(int)(face->size->metrics.descender >> 6);
        fm->x_height   = size / 2;
        fm->draw_spaces = true;
    } else if (fm) {
        fm->height = size + 4;
        fm->ascent = size;
        fm->descent = 4;
        fm->x_height = size / 2;
        fm->draw_spaces = true;
    }

    FontHandle* fh = new FontHandle();
    fh->face       = face;
    fh->size       = size;
    fh->bold       = (fd.weight >= 600);
    fh->italic     = (fd.style == litehtml::font_style_italic);
    fh->underline  = false;
    fh->strikeout  = false;
    return reinterpret_cast<litehtml::uint_ptr>(fh);
}

void SDLContainer::delete_font(litehtml::uint_ptr hFont)
{
    auto* fh = reinterpret_cast<FontHandle*>(hFont);
    if (fh) {
        if (fh->face) FT_Done_Face(fh->face);
        delete fh;
    }
}

int SDLContainer::text_width(const char* text, litehtml::uint_ptr hFont)
{
    if (!text || !hFont) return 0;
    auto* fh = reinterpret_cast<FontHandle*>(hFont);
    if (!fh->face) return (int)strlen(text) * fh->size / 2;

    FT_Set_Pixel_Sizes(fh->face, 0, fh->size);
    int width = 0;
    const char* p = text;
    while (*p) {
        uint32_t cp = utf8_next(p);
        if (FT_Load_Char(fh->face, cp, FT_LOAD_DEFAULT) == 0)
            width += fh->face->glyph->advance.x >> 6;
        else
            width += fh->size / 2;
    }
    return width;
}

void SDLContainer::draw_text(litehtml::uint_ptr hdc, const char* text,
                              litehtml::uint_ptr hFont, litehtml::web_color color,
                              const litehtml::position& pos)
{
    if (!text || !hFont) return;
    auto* fh = reinterpret_cast<FontHandle*>(hFont);
    if (!fh->face) return;

    Color4 col{color.red, color.green, color.blue, color.alpha};
    draw_string(pos.x, pos.y + fh->size, text, fh->face, fh->size, col);
}

void SDLContainer::draw_solid_fill(litehtml::uint_ptr hdc,
                                    const litehtml::background_layer& layer,
                                    const litehtml::web_color& color)
{
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, color.red, color.green, color.blue, color.alpha);
    SDL_Rect r{layer.origin_box.x, layer.origin_box.y,
               layer.origin_box.width, layer.origin_box.height};
    SDL_RenderFillRect(m_renderer, &r);
}

void SDLContainer::draw_linear_gradient(litehtml::uint_ptr hdc,
                                         const litehtml::background_layer& layer,
                                         const litehtml::background_layer::linear_gradient& g)
{
    // Simplified: just fill with first stop color
    if (g.color_points.empty()) return;
    auto& c = g.color_points[0].color;
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, c.red, c.green, c.blue, c.alpha);
    SDL_Rect r{layer.origin_box.x, layer.origin_box.y,
               layer.origin_box.width, layer.origin_box.height};
    SDL_RenderFillRect(m_renderer, &r);
}

void SDLContainer::draw_radial_gradient(litehtml::uint_ptr hdc,
                                         const litehtml::background_layer& layer,
                                         const litehtml::background_layer::radial_gradient& g)
{
    if (g.color_points.empty()) return;
    auto& c = g.color_points[0].color;
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, static_cast<Uint8>(c.red), static_cast<Uint8>(c.green),
                            static_cast<Uint8>(c.blue), static_cast<Uint8>(c.alpha));
    SDL_Rect r{static_cast<int>(layer.origin_box.x), static_cast<int>(layer.origin_box.y),
                static_cast<int>(layer.origin_box.width), static_cast<int>(layer.origin_box.height)};
    SDL_RenderFillRect(m_renderer, &r);
}

void SDLContainer::draw_conic_gradient(litehtml::uint_ptr hdc,
                                        const litehtml::background_layer& layer,
                                        const litehtml::background_layer::conic_gradient& g)
{
    // Fallback: transparent
}

void SDLContainer::draw_borders(litehtml::uint_ptr hdc,
                                 const litehtml::borders& borders,
                                 const litehtml::position& pos,
                                 bool root)
{
    auto draw_border_side = [&](const litehtml::border& b,
                                 int x1, int y1, int x2, int y2) {
        if (b.width <= 0 || b.style == litehtml::border_style_none) return;
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer,
            b.color.red, b.color.green, b.color.blue, b.color.alpha);
        for (int i = 0; i < b.width; i++) {
            SDL_RenderDrawLine(m_renderer, x1, y1 + i, x2, y2 + i);
        }
    };

    draw_border_side(borders.top,
        pos.x, pos.y, pos.x + pos.width, pos.y);
    draw_border_side(borders.bottom,
        pos.x, pos.y + pos.height, pos.x + pos.width, pos.y + pos.height);

    // Left/Right borders as vertical
    if (borders.left.width > 0 && borders.left.style != litehtml::border_style_none) {
        SDL_SetRenderDrawColor(m_renderer,
            borders.left.color.red, borders.left.color.green,
            borders.left.color.blue, borders.left.color.alpha);
        for (int i = 0; i < borders.left.width; i++)
            SDL_RenderDrawLine(m_renderer, pos.x + i, pos.y, pos.x + i, pos.y + pos.height);
    }
    if (borders.right.width > 0 && borders.right.style != litehtml::border_style_none) {
        SDL_SetRenderDrawColor(m_renderer,
            borders.right.color.red, borders.right.color.green,
            borders.right.color.blue, borders.right.color.alpha);
        for (int i = 0; i < borders.right.width; i++)
            SDL_RenderDrawLine(m_renderer,
                pos.x + pos.width - i, pos.y,
                pos.x + pos.width - i, pos.y + pos.height);
    }
}

void SDLContainer::draw_list_marker(litehtml::uint_ptr hdc,
                                     const litehtml::list_marker& marker)
{
    if (marker.marker_type == litehtml::list_style_type_none) return;
    SDL_SetRenderDrawColor(m_renderer,
        marker.color.red, marker.color.green, marker.color.blue, marker.color.alpha);
    SDL_Rect r{marker.pos.x, marker.pos.y + marker.pos.height / 2 - 3, 6, 6};
    SDL_RenderFillRect(m_renderer, &r);
}

void SDLContainer::load_image(const char* src, const char* baseurl, bool redraw)
{
    // Images are loaded on demand in draw_image
}

void SDLContainer::get_image_size(const char* src, const char* baseurl, litehtml::size& sz)
{
    if (!src) return;
    auto it = m_image_cache.find(src);
    if (it != m_image_cache.end()) {
        sz.width  = it->second.w;
        sz.height = it->second.h;
    }
}

void SDLContainer::draw_image(litehtml::uint_ptr hdc,
                               const litehtml::background_layer& layer,
                               const std::string& url,
                               const std::string& base_url)
{
    // Images deferred — just show a light placeholder box
    SDL_SetRenderDrawColor(m_renderer, 60, 60, 65, 100);
    SDL_Rect r{static_cast<int>(layer.border_box.x), static_cast<int>(layer.border_box.y),
                static_cast<int>(layer.border_box.width), static_cast<int>(layer.border_box.height)};
    SDL_RenderDrawRect(m_renderer, &r);
}

void SDLContainer::set_clip(const litehtml::position& pos,
                             const litehtml::border_radiuses& br)
{
    SDL_Rect r{static_cast<int>(pos.x), static_cast<int>(pos.y),
                static_cast<int>(pos.width), static_cast<int>(pos.height)};
    m_clip_stack.push_back(r);
    set_sdl_clip();
}

void SDLContainer::del_clip()
{
    if (!m_clip_stack.empty()) m_clip_stack.pop_back();
    set_sdl_clip();
}

void SDLContainer::set_sdl_clip()
{
    if (m_clip_stack.empty()) {
        SDL_RenderSetClipRect(m_renderer, nullptr);
    } else {
        SDL_RenderSetClipRect(m_renderer, &m_clip_stack.back());
    }
}

void SDLContainer::get_media_features(litehtml::media_features& mf) const
{
    mf.type          = litehtml::media_type_screen;
    mf.width         = m_width;
    mf.height        = m_height;
    mf.device_width  = m_width;
    mf.device_height = m_height;
    mf.color         = 8;
    mf.monochrome    = 0;
    mf.color_index   = 256;
    mf.resolution    = 96;
}

void SDLContainer::transform_text(litehtml::string& text, litehtml::text_transform tt)
{
    switch (tt) {
    case litehtml::text_transform_uppercase:
        for (auto& c : text) c = toupper((unsigned char)c);
        break;
    case litehtml::text_transform_lowercase:
        for (auto& c : text) c = tolower((unsigned char)c);
        break;
    case litehtml::text_transform_capitalize:
        if (!text.empty()) text[0] = toupper((unsigned char)text[0]);
        break;
    default: break;
    }
}
