#include "../ds4_image.c"
#include <assert.h>
#include <time.h>

/* Scalar accumulation order before tap caching, for differential checks. */
static void reference_resize(
        const uint8_t *src,
        uint32_t src_width,
        uint32_t src_height,
        float *dst,
        uint32_t dst_width,
        uint32_t dst_height,
        uint32_t dst_stride, double *unrounded) {
    double scale_x = (double)src_width / dst_width;
    double scale_y = (double)src_height / dst_height;
    double filter_x = scale_x >= 1.0 ? 1.0 / scale_x : 1.0;
    double filter_y = scale_y >= 1.0 ? 1.0 / scale_y : 1.0;
    double support_x = scale_x >= 1.0 ? 2.0 * scale_x : 2.0;
    double support_y = scale_y >= 1.0 ? 2.0 * scale_y : 2.0;

    for (uint32_t dy = 0; dy < dst_height; dy++) {
        double center_y = scale_y * ((double)dy + 0.5);
        int y0 = (int)(center_y - support_y + 0.5);
        int y1 = (int)(center_y + support_y + 0.5);
        if (y0 < 0) y0 = 0;
        if (y1 > (int)src_height) y1 = (int)src_height;
        for (uint32_t dx = 0; dx < dst_width; dx++) {
            double center_x = scale_x * ((double)dx + 0.5);
            int x0 = (int)(center_x - support_x + 0.5);
            int x1 = (int)(center_x + support_x + 0.5);
            if (x0 < 0) x0 = 0;
            if (x1 > (int)src_width) x1 = (int)src_width;
            double sum[3] = {0, 0, 0};
            double weight_sum = 0;
            for (int iy = y0; iy < y1; iy++) {
                double wy = ds4_cubic(((double)iy + 0.5 - center_y) * filter_y,
                                      -0.5);
                for (int ix = x0; ix < x1; ix++) {
                    double wx = ds4_cubic(((double)ix + 0.5 - center_x) * filter_x,
                                          -0.5);
                    double weight = wx * wy;
                    const uint8_t *pixel = src +
                        ((size_t)iy * src_width + (uint32_t)ix) * 3;
                    sum[0] += pixel[0] * weight;
                    sum[1] += pixel[1] * weight;
                    sum[2] += pixel[2] * weight;
                    weight_sum += weight;
                }
            }
            float *pixel = dst + ((size_t)dy * dst_stride + dx) * 3;
            for (unsigned c = 0; c < 3; c++) {
                if (unrounded)
                    unrounded[((size_t)dy * dst_stride + dx) * 3 + c] = sum[c] / weight_sum;
                double value = round(sum[c] / weight_sum);
                if (value < 0.0) value = 0.0;
                if (value > 255.0) value = 255.0;
                pixel[c] = (float)value;
            }
        }
    }
}


static double seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void check_resize(uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
                         unsigned pattern, bool benchmark) {
    size_t pixels = (size_t)sw * sh;
    uint8_t *src = malloc(pixels * 3);
    uint32_t stride = dw + 7;
    size_t values = (size_t)stride * dh * 3;
    float *ref = malloc(values * sizeof(*ref));
    float *out = malloc(values * sizeof(*out));
    double *unrounded = malloc(values * sizeof(*unrounded));
    assert(src && ref && out && unrounded);
    uint32_t rng = 17;
    for (uint32_t y = 0; y < sh; y++) {
        for (uint32_t x = 0; x < sw; x++) {
            for (unsigned c = 0; c < 3; c++) {
                rng = rng * 1664525u + 1013904223u;
                src[((size_t)y * sw + x) * 3 + c] =
                    pattern == 0 ? (uint8_t)(rng >> 24) :
                    pattern == 1 ? (((x + y) & 1) ? 255 : 0) :
                    pattern == 2 ? ((x & 1) ? 255 : 0) : 73;
            }
        }
    }
    for (size_t i = 0; i < values; i++) unrounded[i] = ref[i] = out[i] = -77;
    reference_resize(src, sw, sh, ref, dw, dh, stride, unrounded);
    assert(ds4_resize_rgb_bicubic(src, sw, sh, out, dw, dh, stride));
    if (memcmp(ref, out, values * sizeof(*ref))) {
        size_t different = 0;
        for (size_t i = 0; i < values; i++) {
            if (ref[i] == out[i]) continue;
            different++;
            float gap = fabsf(ref[i] - out[i]);
            double tie_distance = fabs(unrounded[i] - floor(unrounded[i]) - 0.5);
            /* Fast-math may reassociate sums at half-integer rounding ties.
             * Permit only that discontinuity, never a general pixel tolerance. */
            if (gap != 1 || tie_distance > 1e-8) {
                fprintf(stderr, "resize mismatch at %zu: ref=%g cached=%g raw=%.17g\n",
                        i, ref[i], out[i], unrounded[i]);
                abort();
            }
        }
        printf("resize %ux%u -> %ux%u pattern=%u: %zu rounding-tie differences\n",
               sw, sh, dw, dh, pattern, different);
    }
    if (benchmark) {
        double old_time = 0, new_time = 0;
        for (unsigned iteration = 0; iteration < 8; iteration++) {
            for (unsigned arm = 0; arm < 2; arm++) {
                bool optimized = (arm ^ (iteration & 1)) != 0;
                double begin = seconds();
                if (optimized)
                    assert(ds4_resize_rgb_bicubic(src, sw, sh, out, dw, dh, stride));
                else
                    reference_resize(src, sw, sh, ref, dw, dh, stride, NULL);
                double elapsed = seconds() - begin;
                if (optimized) new_time += elapsed;
                else old_time += elapsed;
            }
        }
        printf("%ux%u -> %ux%u: reference=%.3f ms cached=%.3f ms speedup=%.2fx\n",
               sw, sh, dw, dh, old_time * 125, new_time * 125, old_time / new_time);
        assert(!memcmp(ref, out, values * sizeof(*ref)));
    }
    free(out);
    free(ref);
    free(unrounded);
    free(src);
}

int main(int argc, char **argv) {
    bool benchmark = argc == 2 && !strcmp(argv[1], "--bench");
    static const uint32_t shapes[][4] = {
        {4032,3024,532,392}, {2560,1440,532,294}, {1280,720,532,294},
        {640,480,448,336}, {17,9,529,280}, {512,512,170,170},
        {512,512,341,341}, {900,600,300,200}, {613,409,391,261},
        {16384,4,1036,1}, {4,16384,1,574}, {1,1,532,280},
        {37,19,1,1}, {1,257,17,391}, {257,1,391,17}
    };
    for (size_t i = 0; i < sizeof(shapes) / sizeof(*shapes); i++) {
        for (unsigned pattern = 0; pattern < (benchmark ? 1u : 4u); pattern++)
            check_resize(shapes[i][0], shapes[i][1], shapes[i][2], shapes[i][3],
                         pattern, benchmark);
    }
    if (!benchmark) {
        uint32_t seed = 31;
        for (int i = 0; i < 128; i++) {
            uint32_t shape[4];
            for (unsigned d = 0; d < 4; d++) {
                seed = seed * 1664525u + 1013904223u;
                shape[d] = 1 + (seed >> 16) % 600;
            }
            check_resize(shape[0], shape[1], shape[2], shape[3], i % 4, false);
        }
    }
    puts("image resize: differential checks passed (only half-integer rounding ties may differ)");
    return 0;
}
