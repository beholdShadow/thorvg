/*
 * Copyright (c) 2021 - 2025 the ThorVG project. All rights reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "tvgJpgLoader.h"
#include "tvgStr.h"
#include "utils/stbimageutils.h"
#include <cstring>

// stbi functions are already implemented in stbimageutils.cpp
// We just need to declare them
extern "C" {
    int stbi_info(const char *filename, int *x, int *y, int *channels_in_file);
    int stbi_info_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *channels_in_file);
    unsigned char *stbi_load(const char *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
}

/************************************************************************/
/* Internal Class Implementation                                        */
/************************************************************************/

void JpgLoader::clear()
{
    if (freeData) tvg::free(data);
    data = nullptr;
    size = 0;
    freeData = false;
}

/************************************************************************/
/* External Class Implementation                                        */
/************************************************************************/

JpgLoader::JpgLoader() : ImageLoader(FileType::Jpg)
{
}


JpgLoader::~JpgLoader()
{
    clear();
    // Free the RGBA buffer allocated by stbi_load_from_memory
    if (surface.buf8) {
        stbi_image_free(surface.buf8);
        surface.buf8 = nullptr;
    }
}


bool JpgLoader::open(const char* path)
{
#ifdef THORVG_FILE_IO_SUPPORT
    // Get image size directly from file using stbi_info
    int width, height, channels;
    if (!stbi_info(path, &width, &height, &channels)) {
        return false;
    }

    w = static_cast<float>(width);
    h = static_cast<float>(height);

    // Clear previous data
    if (freeData) tvg::free(data);

    // Store file path in data, size = 0 indicates it's a file path
    data = reinterpret_cast<unsigned char*>(duplicate(path));
    if (!data) return false;
    size = 0;  // size = 0 means file path
    freeData = true;  // Need to free the path string

    return true;
#else
    return false;
#endif
}


bool JpgLoader::open(const char* data, uint32_t size, TVG_UNUSED const char* rpath, bool copy)
{
    // Get image size using stbi_info_from_memory
    int width, height, channels;
    if (!stbi_info_from_memory((const unsigned char*)data, size, &width, &height, &channels)) {
        return false;
    }

    // Clear previous data
    if (freeData) tvg::free(this->data);

    if (copy) {
        this->data = tvg::malloc<unsigned char>(size);
        if (!this->data) return false;
        memcpy((unsigned char *)this->data, data, size);
        freeData = true;
    } else {
        this->data = (unsigned char *) data;
        freeData = false;
    }

    w = static_cast<float>(width);
    h = static_cast<float>(height);
    this->size = size;  // size > 0 means memory data

    return true;
}


bool JpgLoader::read()
{
    if (!LoadModule::read()) return true;

    if (w == 0 || h == 0) return false;

    // Free previous buffer if exists
    if (surface.buf8) {
        stbi_image_free(surface.buf8);
        surface.buf8 = nullptr;
    }

    // Load image data: from file if size == 0 (data is file path), otherwise from memory
    int width, height, channels;
    unsigned char* rgbaData = nullptr;
    
    if (size == 0 && data) {
        // size == 0 means data is a file path, load directly from file using stbi_load
        rgbaData = stbi_load(reinterpret_cast<const char*>(data), &width, &height, &channels, 4);
    } else if (data && size > 0) {
        // size > 0 means data is memory data, load from memory using stbi_load_from_memory
        rgbaData = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    }
    
    if (!rgbaData) return false;

    // stbi_load/stbi_load_from_memory returns RGBA format (R, G, B, A)
    // We need to convert to ARGB8888 (B, G, R, A) or ABGR8888 (R, G, B, A) based on color space
    const int pixelCount = width * height;
    
    if (ImageLoader::cs == ColorSpace::ARGB8888 || ImageLoader::cs == ColorSpace::ARGB8888S) {
        // Convert RGBA to ARGB8888 (B, G, R, A)
        for (int i = 0; i < pixelCount; ++i) {
            unsigned char r = rgbaData[i * 4 + 0];
            unsigned char g = rgbaData[i * 4 + 1];
            unsigned char b = rgbaData[i * 4 + 2];
            unsigned char a = rgbaData[i * 4 + 3];
            // ARGB8888: B, G, R, A
            rgbaData[i * 4 + 0] = b;
            rgbaData[i * 4 + 1] = g;
            rgbaData[i * 4 + 2] = r;
            rgbaData[i * 4 + 3] = a;
        }
        surface.cs = ColorSpace::ARGB8888;
    } else {
        // ABGR8888: R, G, B, A (same as RGBA from stbi)
        surface.cs = ColorSpace::ABGR8888;
    }

    // Setup the surface
    surface.buf8 = rgbaData;
    surface.stride = w;
    surface.w = w;
    surface.h = h;
    surface.channelSize = sizeof(uint32_t);
    surface.premultiplied = true;

    clear();
    return true;
}
