#ifndef REF_S3TC_H
#define REF_S3TC_H
#include <stdbool.h>
#include <stdint.h>

/* The pipe_format members bc_decode_s3tc switches on. The real enum lives in Mesa's u_format.h,
 * which this test does not build; the decoder only compares fmt for equality, so these just have
 * to be distinct. Keeping them here (rather than pulling in u_format.h) is what lets the test run
 * from a Mesa checkout without a configured build dir. */
enum pipe_format {
   PIPE_FORMAT_DXT1_RGB = 0,
   PIPE_FORMAT_DXT1_RGBA,
   PIPE_FORMAT_DXT3_RGBA,
   PIPE_FORMAT_DXT5_RGBA,
};

/* util_format_get_blocksize for the four DXT formats: 8 for DXT1, 16 otherwise. */
static inline unsigned
util_format_get_blocksize(enum pipe_format fmt)
{
   return fmt == PIPE_FORMAT_DXT1_RGB || fmt == PIPE_FORMAT_DXT1_RGBA ? 8 : 16;
}

static bool bc_decode_s3tc(enum pipe_format fmt, uint8_t *dst, unsigned dst_row,
                           const uint8_t *src, unsigned src_row, unsigned width,
                           unsigned height);

#endif
