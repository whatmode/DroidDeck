/* Run the shipped BC7 path of the SPIR-V against Mesa's own BC7 decoder.
 *
 * Same rule as test_s3tc.cpp: the shader body below is not written here, it is bc_s3tc.comp
 * compiled by glslangValidator and translated by SPIRV-Cross, both done by run.sh. The reference
 * is decompress_rgba_unorm_block() extracted verbatim from Mesa's texcompress_bptc_tmp.h by
 * genref-bc7.py -- the decoder the CPU path actually runs for BC7, since bc_decode_s3tc() and
 * bc_decode_rgtc() do not handle it.
 *
 * BC7 is where a wrong answer is easiest to miss: the partition and anchor tables decide which
 * subset a texel belongs to and where its index sits, and a table row that is off by one reads a
 * plausible wrong texel. So the whole 64-entry tables are exercised by random blocks, every mode,
 * plus blocks that force the reserved mode and the rotation/index-selection bits.
 *
 * Whole images are decoded, not single blocks, so the destination pitch and the block-to-xy
 * arithmetic are covered too.
 */
#include "bc_s3tc_shader.cpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

/* The reference, extracted verbatim from Mesa's texcompress_bptc_tmp.h by genref-bc7.py. */
#include "ref_bc7.c"

static uint64_t rng_state = 0x243f6a8885a308d3ULL;
static uint32_t
rnd(void)
{
   rng_state ^= rng_state << 13;
   rng_state ^= rng_state >> 7;
   rng_state ^= rng_state << 17;
   return (uint32_t)(rng_state >> 32);
}

/* 4x4 blocks, so block xy is not the linear block index. */
static const unsigned W = 16, H = 16;

static void
shader_decode(uint32_t kind, const uint8_t *src, uint32_t src_row_bytes, uint32_t *dst)
{
   static spirv_cross_shader_t *shader;
   static bool inited = false;
   if (!inited) {
      shader = spirv_cross_construct();
      inited = true;
   }

   unsigned bw = W / 4, bh = H / 4;
   Impl::Shader::Resources::Params p;
   p.kind = kind;
   p.bw = bw;
   p.dst_pitch = W;
   p.dst_off = 0;
   p.src_off = 0;
   p.src_pitch = src_row_bytes / 4;
   p.first = 0;
   p.count = bw * bh;

   void *src_ptr = (void *)src;
   void *dst_ptr = (void *)dst;
   void *pc_ptr = (void *)&p;
   spirv_cross_set_resource(shader, 0, 0, &src_ptr, sizeof(src_ptr));
   spirv_cross_set_resource(shader, 0, 1, &dst_ptr, sizeof(dst_ptr));
   spirv_cross_set_resource(shader, 0, 2, &pc_ptr, sizeof(pc_ptr));

   uvec3 num_wg(1, 1, 1), wg_id(0, 0, 0);
   spirv_cross_set_builtin(shader, SPIRV_CROSS_BUILTIN_NUM_WORK_GROUPS, &num_wg, sizeof(num_wg));
   spirv_cross_set_builtin(shader, SPIRV_CROSS_BUILTIN_WORK_GROUP_ID, &wg_id, sizeof(wg_id));

   spirv_cross_invoke(shader);
}

int
main()
{
   unsigned failures = 0;
   unsigned long cases = 0;
   unsigned bw = W / 4, bh = H / 4;
   unsigned modes_seen = 0;

   for (int iter = 0; iter < 5000; iter++) {
      /* BC7 blocks are 16 bytes; pad the block rows sometimes, which is what bufferRowLength >
       * width does and what a program uploading a sub-rectangle produces. */
      unsigned pad_blocks = ((iter / 4) % 3 == 2) ? 2 : 0;
      unsigned src_row_bytes = (bw + pad_blocks) * 16;
      unsigned comp_size = src_row_bytes * bh;

      std::vector<uint8_t> src(comp_size);
      for (unsigned i = 0; i < comp_size; i++)
         src[i] = (uint8_t)rnd();

      /* The mode is the position of the first set bit in byte 0, so a random byte 0 gives mode 1
       * to 8 in the proportions the bit pattern dictates. Force every mode in turn, and force the
       * reserved mode (byte 0 == 0) so its zero-fill is compared too. */
      unsigned forced = (iter / 4) % 9; /* 0..7 modes 1..8, 8 == reserved */
      for (unsigned y = 0; y < bh; y++) {
         for (unsigned b = 0; b < bw; b++) {
            unsigned off = y * src_row_bytes + b * 16;
            if (forced < 8) {
               /* first set bit at position `forced`: bit 0 is mode 1. */
               src[off + 0] = (uint8_t)((src[off + 0] & ~((1u << (forced + 1)) - 1u)) |
                                        (1u << forced));
            } else {
               src[off + 0] = 0;
            }
         }
      }

      /* The reserved mode is the only one the reference decodes as zeroes; everything else must
       * match. Track which modes the random data actually exercised. */
      for (unsigned y = 0; y < bh; y++) {
         for (unsigned b = 0; b < bw; b++) {
            uint8_t b0 = src[y * src_row_bytes + b * 16];
            unsigned mode = 0;
            for (unsigned bit = 0; bit < 8; bit++)
               if (b0 & (1u << bit)) {
                  mode = bit + 1;
                  break;
               }
            if (mode)
               modes_seen |= 1u << (mode - 1);
         }
      }

      /* Reference: Mesa's decoder, whole image, the same padded rows the shader gets. */
      std::vector<uint8_t> ref((size_t)W * H * 4, 0xcd);
      decompress_rgba_unorm(W, H, src.data(), src_row_bytes, ref.data(), W * 4);

      /* Shader: the same image through kind 4. */
      std::vector<uint32_t> dst((size_t)W * H, 0xcdcdcdcdu);
      shader_decode(4, src.data(), src_row_bytes, dst.data());

      std::vector<uint8_t> got((size_t)W * H * 4);
      memcpy(got.data(), dst.data(), got.size());

      for (unsigned y = 0; y < H; y++) {
         for (unsigned x = 0; x < W; x++) {
            const uint8_t *r = &ref[((size_t)y * W + x) * 4];
            const uint8_t *g = &got[((size_t)y * W + x) * 4];
            cases++;
            if (memcmp(r, g, 4) != 0) {
               if (failures < 10)
                  printf("MISMATCH at (%u,%u) iter=%d ref=%02x%02x%02x%02x got=%02x%02x%02x%02x\n",
                         x, y, iter, r[0], r[1], r[2], r[3], g[0], g[1], g[2], g[3]);
               failures++;
            }
         }
      }
   }

   printf("%lu BC7 pixels compared, %u mismatches, modes seen 0x%02x\n", cases, failures,
          modes_seen);
   return failures ? 1 : 0;
}
