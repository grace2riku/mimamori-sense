# puff (zlib contrib)

Unmodified `puff.c` and `puff.h` from zlib v1.3.1, by Mark Adler.
License and redistribution conditions are retained in `puff.h`.

- https://github.com/madler/zlib/tree/v1.3.1/contrib/puff
- puff.c SHA256: `5b9d75aeb5baf3575415bc6ade3f2a02e50b6b971b3f8b4fda2b03543bc6e52f`
- puff.h SHA256: `969b7be2a930db0cdcb19b0e5b29ae6741f5a8f663b6dba6d647e12ec60cfa8e`

Only `ui_startup_image.c` calls puff. The caller provides a fixed-size SDRAM
output buffer and validates the zlib header, consumed/produced lengths, and
Adler-32 trailer. There is no dynamic allocation. Calls are serialized by the
LVGL task (puff's fixed Huffman tables are lazily initialized).
