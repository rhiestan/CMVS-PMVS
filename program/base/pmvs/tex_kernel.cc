#include "tex_kernel.h"

#include <cmath>

#ifdef PMVS_USE_HIGHWAY
#include "hwy/highway.h"
#endif

namespace PMVS3 {
namespace TexKernel {

#ifdef PMVS_USE_HIGHWAY

namespace hn = hwy::HWY_NAMESPACE;

// 4 floats: the same result on every instruction set.
using DF = hn::FixedTag<float, 4>;
using DI = hn::FixedTag<int32_t, 4>;
using D8 = hn::FixedTag<uint8_t, 4>;

static HWY_INLINE hn::Vec<DF> LoadRgbx(const unsigned char* p) {
  // Four bytes -> four floats; the fourth lane is not used.
  const DF df;
  const DI di;
  const D8 d8;
  return hn::ConvertTo(df, hn::PromoteTo(di, hn::LoadU(d8, p)));
}

void SampleWindow(const unsigned char* image, int width, float x, float y,
                  float dxx, float dxy, float dyx, float dyy, int size,
                  float* out) {
  const DF df;
  const int stride = 3 * width;
  for (int r = 0; r < size; ++r) {
    float fx = x;
    float fy = y;
    x += dyx;
    y += dyy;
    for (int c = 0; c < size; ++c) {
      const int lx = static_cast<int>(fx);
      const int ly = static_cast<int>(fy);
      const unsigned char* p0 = image + ly * stride + 3 * lx;
      const unsigned char* p1 = p0 + stride;

      const float dx1 = fx - lx;
      const float dx0 = 1.0f - dx1;
      const float dy1 = fy - ly;
      const float dy0 = 1.0f - dy1;

      // 4 bytes are loaded for each (rgb) pixel; the reads reach one byte
      // behind the pixel to the right, inside the margin of the window.
      const auto t = hn::Add(hn::Mul(LoadRgbx(p0), hn::Set(df, dx0 * dy0)),
                             hn::Mul(LoadRgbx(p1), hn::Set(df, dx0 * dy1)));
      const auto u = hn::Add(hn::Mul(LoadRgbx(p0 + 3), hn::Set(df, dx1 * dy0)),
                             hn::Mul(LoadRgbx(p1 + 3), hn::Set(df, dx1 * dy1)));
      const auto rgbx = hn::Add(t, u);
      if (r == size - 1 && c == size - 1) {
        // The last texel must not write behind the output.
        HWY_ALIGN float tmp[4];
        hn::Store(rgbx, df, tmp);
        out[0] = tmp[0];
        out[1] = tmp[1];
        out[2] = tmp[2];
      } else {
        // Writes 4 floats; the fourth is overwritten by the next texel.
        hn::StoreU(rgbx, df, out);
      }
      out += 3;

      fx += dxx;
      fy += dxy;
    }
  }
}

void Normalize(float* tex, int n) {
  const DF df;
  const int n3 = n / 3;

  // Channel sums. 12 floats = 4 pixels = 3 vectors; the lanes of the three
  // accumulators cycle through the channels.
  auto s0 = hn::Zero(df), s1 = hn::Zero(df), s2 = hn::Zero(df);
  int i = 0;
  for (; i + 12 <= n; i += 12) {
    s0 = hn::Add(s0, hn::LoadU(df, tex + i));
    s1 = hn::Add(s1, hn::LoadU(df, tex + i + 4));
    s2 = hn::Add(s2, hn::LoadU(df, tex + i + 8));
  }
  HWY_ALIGN float a0[4], a1[4], a2[4];
  hn::Store(s0, df, a0);
  hn::Store(s1, df, a1);
  hn::Store(s2, df, a2);
  float ave[3] = {a0[0] + a0[3] + a1[2] + a2[1],
                  a0[1] + a1[0] + a1[3] + a2[2],
                  a0[2] + a1[1] + a2[0] + a2[3]};
  for (; i < n; i += 3) {
    ave[0] += tex[i];
    ave[1] += tex[i + 1];
    ave[2] += tex[i + 2];
  }
  ave[0] /= n3;
  ave[1] /= n3;
  ave[2] /= n3;

  // Sum of squared differences to the mean.
  const float m0[4] = {ave[0], ave[1], ave[2], ave[0]};
  const float m1[4] = {ave[1], ave[2], ave[0], ave[1]};
  const float m2[4] = {ave[2], ave[0], ave[1], ave[2]};
  const auto v0 = hn::LoadU(df, m0);
  const auto v1 = hn::LoadU(df, m1);
  const auto v2 = hn::LoadU(df, m2);
  auto q = hn::Zero(df);
  i = 0;
  for (; i + 12 <= n; i += 12) {
    const auto d0 = hn::Sub(v0, hn::LoadU(df, tex + i));
    const auto d1 = hn::Sub(v1, hn::LoadU(df, tex + i + 4));
    const auto d2 = hn::Sub(v2, hn::LoadU(df, tex + i + 8));
    q = hn::Add(q, hn::Add(hn::Mul(d0, d0), hn::Add(hn::Mul(d1, d1), hn::Mul(d2, d2))));
  }
  float ave2 = hn::ReduceSum(df, q);
  for (; i < n; i += 3) {
    const float f0 = ave[0] - tex[i];
    const float f1 = ave[1] - tex[i + 1];
    const float f2 = ave[2] - tex[i + 2];
    ave2 += f0 * f0 + f1 * f1 + f2 * f2;
  }
  ave2 = std::sqrt(ave2 / n);
  if (ave2 == 0.0f)
    ave2 = 1.0f;

  // (value - mean) / deviation (a true division, as before).
  const auto vs = hn::Set(df, ave2);
  i = 0;
  for (; i + 12 <= n; i += 12) {
    hn::StoreU(hn::Div(hn::Sub(hn::LoadU(df, tex + i), v0), vs), df, tex + i);
    hn::StoreU(hn::Div(hn::Sub(hn::LoadU(df, tex + i + 4), v1), vs), df, tex + i + 4);
    hn::StoreU(hn::Div(hn::Sub(hn::LoadU(df, tex + i + 8), v2), vs), df, tex + i + 8);
  }
  for (; i < n; i += 3) {
    tex[i] = (tex[i] - ave[0]) / ave2;
    tex[i + 1] = (tex[i + 1] - ave[1]) / ave2;
    tex[i + 2] = (tex[i + 2] - ave[2]) / ave2;
  }
}

float Dot(const float* a, const float* b, int n) {
  const DF df;
  auto s0 = hn::Zero(df), s1 = hn::Zero(df);
  int i = 0;
  for (; i + 8 <= n; i += 8) {
    s0 = hn::Add(s0, hn::Mul(hn::LoadU(df, a + i), hn::LoadU(df, b + i)));
    s1 = hn::Add(s1, hn::Mul(hn::LoadU(df, a + i + 4), hn::LoadU(df, b + i + 4)));
  }
  float ans = hn::ReduceSum(df, hn::Add(s0, s1));
  for (; i < n; ++i)
    ans += a[i] * b[i];
  return ans / n;
}

#else  // PMVS_USE_HIGHWAY

void SampleWindow(const unsigned char* image, int width, float x, float y,
                  float dxx, float dxy, float dyx, float dyy, int size,
                  float* out) {
  const int stride = 3 * width;
  for (int r = 0; r < size; ++r) {
    float fx = x;
    float fy = y;
    x += dyx;
    y += dyy;
    for (int c = 0; c < size; ++c) {
      const int lx = static_cast<int>(fx);
      const int ly = static_cast<int>(fy);
      const unsigned char* p0 = image + ly * stride + 3 * lx;
      const unsigned char* p1 = p0 + stride;
      const float dx1 = fx - lx;
      const float dx0 = 1.0f - dx1;
      const float dy1 = fy - ly;
      const float dy0 = 1.0f - dy1;
      const float f00 = dx0 * dy0, f01 = dx0 * dy1;
      const float f10 = dx1 * dy0, f11 = dx1 * dy1;
      for (int k = 0; k < 3; ++k)
        out[k] = (p0[k] * f00 + p1[k] * f01) + (p0[k + 3] * f10 + p1[k + 3] * f11);
      out += 3;
      fx += dxx;
      fy += dxy;
    }
  }
}

void Normalize(float* tex, int n) {
  const int n3 = n / 3;
  float ave[3] = {0.0f, 0.0f, 0.0f};
  for (int i = 0; i < n; i += 3) {
    ave[0] += tex[i];
    ave[1] += tex[i + 1];
    ave[2] += tex[i + 2];
  }
  ave[0] /= n3;
  ave[1] /= n3;
  ave[2] /= n3;
  float ave2 = 0.0f;
  for (int i = 0; i < n; i += 3) {
    const float f0 = ave[0] - tex[i];
    const float f1 = ave[1] - tex[i + 1];
    const float f2 = ave[2] - tex[i + 2];
    ave2 += f0 * f0 + f1 * f1 + f2 * f2;
  }
  ave2 = std::sqrt(ave2 / n);
  if (ave2 == 0.0f)
    ave2 = 1.0f;
  for (int i = 0; i < n; i += 3) {
    tex[i] = (tex[i] - ave[0]) / ave2;
    tex[i + 1] = (tex[i + 1] - ave[1]) / ave2;
    tex[i + 2] = (tex[i + 2] - ave[2]) / ave2;
  }
}

float Dot(const float* a, const float* b, int n) {
  float ans = 0.0f;
  for (int i = 0; i < n; ++i)
    ans += a[i] * b[i];
  return ans / n;
}

#endif  // PMVS_USE_HIGHWAY

}  // namespace TexKernel
}  // namespace PMVS3
