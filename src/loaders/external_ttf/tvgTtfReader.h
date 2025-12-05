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

#ifndef _TVG_EXTERNAL_TTF_READER_H
#define _TVG_EXTERNAL_TTF_READER_H

#include <atomic>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include "tvgRender.h"

#define INVALID_GLYPH ((uint32_t)-1)

// Reuse the same structures from TtfReader
struct TtfGlyph
{
    uint32_t idx;        //glyph index
    float advance;       //advance width/height
    float lsb;           //left side bearing
    float y;             //y-offset
    float w, h;          //bounding box
};

struct TtfGlyphMetrics : TtfGlyph
{
    RenderPath path;     //outline path
};

struct TtfReader
{
public:
    uint8_t* data = nullptr;  // Keep for compatibility, but not used
    uint32_t size = 0;        // Keep for compatibility, but not used

    struct
    {
        //horizontal header info
        struct {
            float ascent, descent;
            float lineGap;
            float advance;
        } hhea;

        uint16_t unitsPerEm;
        uint16_t numHmtx;      //the number of Horizontal metrics table
        uint8_t locaFormat;    //0 for short offsets, 1 for long (not used with FreeType)
    } metrics;

    TtfReader();
    ~TtfReader();

    bool header();
    uint32_t glyph(uint32_t codepoint, TtfGlyphMetrics* tgm);
    bool kerning(uint32_t lglyph, uint32_t rglyph, Point& out);
    bool convert(RenderPath& path, TtfGlyph& glyph, uint32_t glyphOffset, const Point& offset, uint16_t depth);

    // Additional methods for initialization
    bool init(const char* path);
    bool init(const char* data, uint32_t size, bool copy);
    void cleanup();

private:
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    uint8_t* fontData = nullptr;
    uint32_t fontDataSize = 0;
    bool freeData = false;

    // FreeType outline callback context
    struct OutlineContext
    {
        RenderPath* path;
        Point offset;
        Point lastPoint;
        bool hasMoveTo;
    };

    static int ftOutlineMoveTo(const FT_Vector* to, void* user);
    static int ftOutlineLineTo(const FT_Vector* to, void* user);
    static int ftOutlineConicTo(const FT_Vector* control, const FT_Vector* to, void* user);
    static int ftOutlineCubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user);

    uint32_t glyphMetrics(TtfGlyph& glyph);
    bool initFreeType();
    void cleanupFreeType();
};

#endif //_TVG_EXTERNAL_TTF_READER_H

