/*
 * Copyright (c) 2023 - 2025 the ThorVG project. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "tvgTtfReader.h"
#include "tvgCommon.h"

/************************************************************************/
/* FreeType Outline Callbacks                                          */
/************************************************************************/

int TtfReader::ftOutlineMoveTo(const FT_Vector* to, void* user)
{
    auto* ctx = static_cast<OutlineContext*>(user);
    // Convert from 26.6 fixed point to float, and flip Y axis (FreeType uses bottom-up)
    Point pt{ctx->offset.x + to->x / 64.0f, ctx->offset.y - to->y / 64.0f};
    ctx->path->moveTo(pt);
    ctx->lastPoint = pt;
    ctx->hasMoveTo = true;
    return 0;
}

int TtfReader::ftOutlineLineTo(const FT_Vector* to, void* user)
{
    auto* ctx = static_cast<OutlineContext*>(user);
    Point pt{ctx->offset.x + to->x / 64.0f, ctx->offset.y - to->y / 64.0f};
    ctx->path->lineTo(pt);
    ctx->lastPoint = pt;
    return 0;
}

int TtfReader::ftOutlineConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
{
    auto* ctx = static_cast<OutlineContext*>(user);
    Point ctrl{ctx->offset.x + control->x / 64.0f, ctx->offset.y - control->y / 64.0f};
    Point end{ctx->offset.x + to->x / 64.0f, ctx->offset.y - to->y / 64.0f};
    
    // Convert quadratic to cubic: Q(c, e) = C(p, 2/3*c + 1/3*p, 2/3*c + 1/3*e, e)
    Point cp1 = ctx->lastPoint + (2.0f / 3.0f) * (ctrl - ctx->lastPoint);
    Point cp2 = end + (2.0f / 3.0f) * (ctrl - end);
    ctx->path->cubicTo(cp1, cp2, end);
    ctx->lastPoint = end;
    return 0;
}

int TtfReader::ftOutlineCubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user)
{
    auto* ctx = static_cast<OutlineContext*>(user);
    Point cp1{ctx->offset.x + control1->x / 64.0f, ctx->offset.y - control1->y / 64.0f};
    Point cp2{ctx->offset.x + control2->x / 64.0f, ctx->offset.y - control2->y / 64.0f};
    Point end{ctx->offset.x + to->x / 64.0f, ctx->offset.y - to->y / 64.0f};
    ctx->path->cubicTo(cp1, cp2, end);
    ctx->lastPoint = end;
    return 0;
}

/************************************************************************/
/* External Class Implementation                                       */
/************************************************************************/

TtfReader::TtfReader()
{
    initFreeType();
}

TtfReader::~TtfReader()
{
    cleanup();
}

bool TtfReader::initFreeType()
{
    if (library) return true;  // Already initialized
    
    FT_Error error = FT_Init_FreeType(&library);
    if (error) {
        TVGERR("TTF", "Failed to initialize FreeType library");
        return false;
    }
    return true;
}

void TtfReader::cleanupFreeType()
{
    if (face) {
        FT_Done_Face(face);
        face = nullptr;
    }
    if (library) {
        FT_Done_FreeType(library);
        library = nullptr;
    }
    if (freeData && fontData) {
        tvg::free(fontData);
        fontData = nullptr;
        fontDataSize = 0;
        freeData = false;
    }
}

bool TtfReader::init(const char* path)
{
    if (!initFreeType()) return false;
    
    FT_Error error = FT_New_Face(library, path, 0, &face);
    if (error) {
        TVGERR("TTF", "Failed to load font from path: %s", path);
        return false;
    }
    
    error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    if (error) {
        TVGERR("TTF", "Failed to select Unicode charmap");
        FT_Done_Face(face);
        face = nullptr;
        return false;
    }
    
    return header();
}

bool TtfReader::init(const char* data, uint32_t size, bool copy)
{
    if (!initFreeType()) return false;
    
    if (copy) {
        fontData = tvg::malloc<uint8_t>(size);
        if (!fontData) return false;
        memcpy(fontData, data, size);
        fontDataSize = size;
        freeData = true;
        data = (const char*)fontData;
    }
    
    FT_Error error = FT_New_Memory_Face(library, (const FT_Byte*)data, size, 0, &face);
    if (error) {
        TVGERR("TTF", "Failed to load font from memory");
        if (freeData) {
            tvg::free(fontData);
            fontData = nullptr;
            fontDataSize = 0;
            freeData = false;
        }
        return false;
    }
    
    error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
    if (error) {
        TVGERR("TTF", "Failed to select Unicode charmap");
        FT_Done_Face(face);
        face = nullptr;
        return false;
    }
    
    return header();
}

void TtfReader::cleanup()
{
    cleanupFreeType();
}

bool TtfReader::header()
{
    if (!face) return false;
    
    // Read font metrics from FreeType face
    metrics.unitsPerEm = face->units_per_EM;
    if (metrics.unitsPerEm == 0) {
        metrics.unitsPerEm = 2048;  // Default fallback
    }
    
    // Get horizontal metrics from face (in font units)
    metrics.hhea.ascent = (float)face->ascender;
    metrics.hhea.descent = (float)face->descender;
    
    // Calculate line gap
    if (face->height > 0) {
        metrics.hhea.lineGap = (float)face->height - (metrics.hhea.ascent - metrics.hhea.descent);
    } else {
        metrics.hhea.lineGap = 0;
    }
    if (metrics.hhea.lineGap < 0) metrics.hhea.lineGap = 0;
    
    // Calculate advance (line height)
    metrics.hhea.advance = metrics.hhea.ascent - metrics.hhea.descent + metrics.hhea.lineGap;
    
    // Get number of horizontal metrics
    metrics.numHmtx = (uint16_t)face->num_glyphs;
    
    // locaFormat is not used with FreeType, but set a default value for compatibility
    metrics.locaFormat = 1;
    
    return true;
}

uint32_t TtfReader::glyph(uint32_t codepoint, TtfGlyphMetrics* tgm)
{
    if (!face || !tgm) return 0;
    
    // Get glyph index from codepoint
    FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
    if (glyphIndex == 0) {
        return 0;  // Glyph not found
    }
    
    tgm->idx = glyphIndex;
    return glyphMetrics(*tgm);
}

uint32_t TtfReader::glyphMetrics(TtfGlyph& glyph)
{
    if (!face) return 0;
    
    // Load glyph metrics
    FT_Error error = FT_Load_Glyph(face, glyph.idx, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP);
    if (error) {
        TVGERR("TTF", "Failed to load glyph %u", glyph.idx);
        return 0;
    }
    
    FT_GlyphSlot slot = face->glyph;
    FT_Glyph_Metrics* metrics = &slot->metrics;
    
    // Convert from 26.6 fixed point to float
    glyph.advance = (float)metrics->horiAdvance / 64.0f;
    glyph.lsb = (float)metrics->horiBearingX / 64.0f;
    
    // Bounding box - use CBox (control box) which is faster and doesn't require outline modification
    FT_BBox bbox;
    if (slot->outline.flags & FT_OUTLINE_NONE) {
        // Empty glyph
        glyph.w = glyph.h = glyph.y = 0.0f;
        return 0;
    }
    
    FT_Outline_Get_CBox(&slot->outline, &bbox);
    glyph.w = (float)(bbox.xMax - bbox.xMin) / 64.0f;
    glyph.h = (float)(bbox.yMax - bbox.yMin) / 64.0f;
    glyph.y = (float)bbox.yMax / 64.0f;
    
    // Return a non-zero value to indicate success (glyphOffset is not used with FreeType)
    return 1;
}

bool TtfReader::convert(RenderPath& path, TtfGlyph& glyph, uint32_t glyphOffset, const Point& offset, uint16_t depth)
{
    if (!face) return false;
    
    // Load glyph outline (use unscaled to get original font units)
    FT_Error error = FT_Load_Glyph(face, glyph.idx, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP);
    if (error) {
        return false;
    }
    
    FT_GlyphSlot slot = face->glyph;
    if (!(slot->format == FT_GLYPH_FORMAT_OUTLINE)) {
        return false;  // Not an outline glyph
    }
    
    FT_Outline* outline = &slot->outline;
    if (outline->n_points == 0) {
        return true;  // Empty glyph
    }
    
    // Setup callback context
    OutlineContext ctx;
    ctx.path = &path;
    ctx.offset = offset;
    ctx.lastPoint = {0.0f, 0.0f};
    ctx.hasMoveTo = false;
    
    // Setup FreeType outline function interface
    static FT_Outline_Funcs funcs = {
        (FT_Outline_MoveToFunc)ftOutlineMoveTo,
        (FT_Outline_LineToFunc)ftOutlineLineTo,
        (FT_Outline_ConicToFunc)ftOutlineConicTo,
        (FT_Outline_CubicToFunc)ftOutlineCubicTo,
        0,  // shift
        0   // delta
    };
    
    // Decompose outline
    error = FT_Outline_Decompose(outline, &funcs, &ctx);
    if (error) {
        TVGERR("TTF", "Failed to decompose outline for glyph %u", glyph.idx);
        return false;
    }
    
    // FreeType automatically handles closing contours, but we need to ensure
    // the path is properly closed. The outline decomposition should have
    // already closed contours, but we check anyway.
    return true;
}

bool TtfReader::kerning(uint32_t lglyph, uint32_t rglyph, Point& out)
{
    if (!face) return false;
    
    // Check if face has kerning
    if (!FT_HAS_KERNING(face)) {
        return false;
    }
    
    FT_Vector kerning;
    FT_Error error = FT_Get_Kerning(face, lglyph, rglyph, FT_KERNING_UNSCALED, &kerning);
    if (error) {
        return false;
    }
    
    // Convert from 26.6 fixed point to float
    out.x += (float)kerning.x / 64.0f;
    out.y += (float)kerning.y / 64.0f;
    
    return true;
}

