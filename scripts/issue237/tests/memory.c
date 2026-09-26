/* Freestanding test support; avoids linking target libc into the emulator. */
#include <stddef.h>
void *memset(void *dst, int value, size_t count) {
    unsigned char *p = dst;
    while (count--) *p++ = (unsigned char)value;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t count) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (count--) *d++ = *s++;
    return dst;
}
void __aeabi_memcpy(void *dst, const void *src, size_t count) { (void)memcpy(dst, src, count); }
void __aeabi_memcpy4(void *dst, const void *src, size_t count) { (void)memcpy(dst, src, count); }
void __aeabi_memclr(void *dst, size_t count) { (void)memset(dst, 0, count); }
void __aeabi_memclr4(void *dst, size_t count) { (void)memset(dst, 0, count); }
