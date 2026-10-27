/* Run the SPIR-V that ships against the CPU decoder it must agree with.
 *
 * The shader body below is NOT written by hand: it is bc_s3tc.comp, compiled to SPIR-V by
 * glslangValidator and translated to C++ by SPIRV-Cross, both done by run.sh. That is the point.
 * bc-band-test/run.sh's header records a copy that drifted from the driver and passed while the
 * phone showed a cut-in-half map, so this test runs the artifact that gets embedded in the patch,
 * not a re-implementation of it.
 *
 * Whole images are decoded here rather than single blocks, so the destination pitch and the
 * block-index-to-xy arithmetic are exercised too.
 */
#include "bc_s3tc_shader.cpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

/* The reference, extracted verbatim from vn_droiddeck_bc.c by genref.py. */
#include "ref_s3tc.c"

/* Deterministic PRNG so a failure is reproducible. */
static uint64_t rng_state = 0x123456789abcdefULL;
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
shader_decode(uint32_t kind, const uint8_t *src, uint32_t *dst)
{
   static spirv_cross_shader_t *shader;
   static bool inited = false;
   if (!inited) {
      /* The constructor registers the shader's own resources and builtins; calling init() again
       * here would register them twice and trip the runtime's assertion. */
      shader = spirv_cross_construct();
      inited = true;
   }

   unsigned bw = W / 4, bh = H / 4;
   Impl::Shader::Resources::PC p;
   p.kind = kind;
   p.bw = bw;
   p.dst_pitch = W;
   p.dst_off = 0;
   p.src_off = 0;
   p.first = 0;
   p.count = bw * bh;

   void *src_ptr = (void *)src;
   void *dst_ptr = dst;
   spirv_cross_set_resource(shader, 0, 0, &src_ptr, sizeof(src_ptr));
   spirv_cross_set_resource(shader, 0, 1, &dst_ptr, sizeof(dst_ptr));
   spirv_cross_set_push_constant(shader, &p, sizeof(p));

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

   for (int iter = 0; iter < 5000; iter++) {
      unsigned kind = iter % 4;
      unsigned block = kind <= 1 ? 8 : 16;
      unsigned comp_size = (size_t)bw * bh * block;

      std::vector<uint8_t> src(comp_size);
      for (unsigned i = 0; i < comp_size; i++)
         src[i] = (uint8_t)rnd();

      /* Force BC1's three-colour mode in some blocks, so that branch is not left to chance: BC1
       * uses three colours when color0 <= color1. */
      if (kind <= 1 && (iter / 4) % 3 == 0) {
         for (unsigned b = 0; b < bw * bh; b++) {
            uint16_t c0 = (uint16_t)(rnd() & 0xffff), c1 = (uint16_t)(rnd() & 0xffff);
            if (c0 > c1) {
               uint16_t t = c0;
               c0 = c1;
               c1 = t;
            }
            src[b * 8 + 0] = c0 & 0xff;
            src[b * 8 + 1] = c0 >> 8;
            src[b * 8 + 2] = c1 & 0xff;
            src[b * 8 + 3] = c1 >> 8;
         }
      }

      enum pipe_format fmt = kind == 0   ? PIPE_FORMAT_DXT1_RGB
                             : kind == 1 ? PIPE_FORMAT_DXT1_RGBA
                             : kind == 2 ? PIPE_FORMAT_DXT3_RGBA
                                         : PIPE_FORMAT_DXT5_RGBA;

      /* Reference: the real decoder, whole image, packed rows. */
      std::vector<uint8_t> ref((size_t)W * H * 4, 0xcd);
      bc_decode_s3tc(fmt, ref.data(), W * 4, src.data(), bw * block, W, H);

      /* Shader: the same image. */
      std::vector<uint32_t> dst((size_t)W * H, 0xcdcdcdcdu);
      shader_decode(kind, src.data(), dst.data());

      std::vector<uint8_t> got((size_t)W * H * 4);
      memcpy(got.data(), dst.data(), got.size());

      for (unsigned y = 0; y < H; y++) {
         for (unsigned x = 0; x < W; x++) {
            const uint8_t *r = &ref[((size_t)y * W + x) * 4];
            const uint8_t *g = &got[((size_t)y * W + x) * 4];
            cases++;
            if (memcmp(r, g, 4) != 0) {
               if (failures < 10)
                  printf("MISMATCH kind=%u at (%u,%u) ref=%02x%02x%02x%02x got=%02x%02x%02x%02x\n",
                         kind, x, y, r[0], r[1], r[2], r[3], g[0], g[1], g[2], g[3]);
               failures++;
            }
         }
      }
   }

   printf("%lu pixels compared, %u mismatches\n", cases, failures);
   return failures ? 1 : 0;
}
