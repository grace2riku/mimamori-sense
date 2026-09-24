/* Decode the built-in startup image without allocating from the LVGL heap. */
#include "ui_startup_image.h"
#include "puff/puff.h"
#include "ui_startup_image_data.inc"

static bool decode_zlib(const uint8_t *data, size_t size, uint8_t *out, size_t expected)
{
    if (data == NULL || out == NULL || size < 6U || expected == 0U ||
        size > UINT32_MAX || expected > UINT32_MAX) {
        return false;
    }
    /* RFC 1950: DEFLATE, <=32KiB window, header check, no preset dictionary. */
    if ((data[0] & 15U) != 8U || (data[0] >> 4) > 7U ||
        (((uint32_t)data[0] << 8) | data[1]) % 31U != 0U ||
        (data[1] & 32U) != 0U) {
        return false;
    }
    unsigned long source_size = (unsigned long)(size - 6U);
    unsigned long output_size = (unsigned long)expected;
    int result = puff(out, &output_size, data + 2U, &source_size);
    if (result != 0 || output_size != expected || source_size != size - 6U) {
        return false;
    }
    uint32_t a = 1U;
    uint32_t b = 0U;
    /* Bounded chunks keep both accumulators in 32 bits. */
    for (size_t offset = 0U; offset < expected;) {
        size_t end = offset + 5552U;
        if (end > expected) end = expected;
        while (offset < end) {
            a += out[offset++];
            b += a;
        }
        a %= 65521U;
        b %= 65521U;
    }
    const uint8_t *tail = data + size - 4U;
    uint32_t checksum = ((uint32_t)tail[0] << 24) | ((uint32_t)tail[1] << 16) |
                        ((uint32_t)tail[2] << 8) | tail[3];
    return ((b << 16) | a) == checksum;
}

bool ui_startup_image_decode(uint8_t *pixels, size_t size)
{
    if (size != UI_STARTUP_IMAGE_BYTES) return false;
    return decode_zlib(s_image_zlib, sizeof(s_image_zlib), pixels, size);
}
