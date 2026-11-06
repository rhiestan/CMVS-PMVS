// Compares the texture kernels with the scalar code they replace.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "tex_kernel.h"

using namespace PMVS3;

// ---- Reference implementations (the original code) ---------------------------

static void RefSample(const std::vector<unsigned char>& img, int width, float x,
                      float y, float dxx, float dxy, float dyx, float dyy,
                      int size, std::vector<float>& tex) {
  tex.resize(3 * size * size);
  float* texp = &tex[0] - 1;
  float lx0 = x, ly0 = y;
  for (int r = 0; r < size; ++r) {
    float fx = lx0, fy = ly0;
    lx0 += dyx;
    ly0 += dyy;
    for (int c = 0; c < size; ++c) {
      const int lx = static_cast<int>(fx);
      const int ly = static_cast<int>(fy);
      const int index = 3 * (ly * width + lx);
      const float dx1 = fx - lx;  const float dx0 = 1.0f - dx1;
      const float dy1 = fy - ly;  const float dy0 = 1.0f - dy1;
      const float f00 = dx0 * dy0;  const float f01 = dx0 * dy1;
      const float f10 = dx1 * dy0;  const float f11 = dx1 * dy1;
      const int index2 = index + 3 * width;
      const unsigned char* ucp0 = &img[index] - 1;
      const unsigned char* ucp1 = &img[index2] - 1;
      float rr = 0.0f, g = 0.0f, b = 0.0f;
      rr += *(++ucp0) * f00 + *(++ucp1) * f01;
      g += *(++ucp0) * f00 + *(++ucp1) * f01;
      b += *(++ucp0) * f00 + *(++ucp1) * f01;
      rr += *(++ucp0) * f10 + *(++ucp1) * f11;
      g += *(++ucp0) * f10 + *(++ucp1) * f11;
      b += *(++ucp0) * f10 + *(++ucp1) * f11;
      *(++texp) = rr;
      *(++texp) = g;
      *(++texp) = b;
      fx += dxx;
      fy += dxy;
    }
  }
}

static void RefNormalize(std::vector<float>& tex) {
  const int size = (int)tex.size();
  const int size3 = size / 3;
  float ave[3] = {0, 0, 0};
  float* texp = &tex[0] - 1;
  for (int i = 0; i < size3; ++i) {
    ave[0] += *(++texp);
    ave[1] += *(++texp);
    ave[2] += *(++texp);
  }
  for (int c = 0; c < 3; ++c) ave[c] /= size3;
  float ave2 = 0.0;
  texp = &tex[0] - 1;
  for (int i = 0; i < size3; ++i) {
    const float f0 = ave[0] - *(++texp);
    const float f1 = ave[1] - *(++texp);
    const float f2 = ave[2] - *(++texp);
    ave2 += f0 * f0 + f1 * f1 + f2 * f2;
  }
  ave2 = sqrt(ave2 / size);
  if (ave2 == 0.0f) ave2 = 1.0f;
  texp = &tex[0] - 1;
  for (int i = 0; i < size3; ++i) {
    *(++texp) -= ave[0];  *texp /= ave2;
    *(++texp) -= ave[1];  *texp /= ave2;
    *(++texp) -= ave[2];  *texp /= ave2;
  }
}

static float RefDot(const std::vector<float>& a, const std::vector<float>& b) {
  const int size = (int)a.size();
  float ans = 0.0f;
  for (int i = 0; i < size; ++i) ans += a[i] * b[i];
  return ans / size;
}

// ---- Tests --------------------------------------------------------------------

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
  std::mt19937 gen(1);
  std::uniform_real_distribution<float> uni(0.0f, 1.0f);

  // Window sampling must be bit-identical. Where the compiler fuses the
  // multiplications and additions of the (scalar) reference into FMA
  // instructions (arm64), the results can differ in the last bits.
#if defined(__aarch64__) || defined(__ARM_FEATURE_FMA)
  const float sample_tol = 1e-4f;
#else
  const float sample_tol = 0.0f;
#endif
  const int width = 120, height = 90;
  std::vector<unsigned char> img(width * height * 3);
  for (auto& v : img) v = (unsigned char)(gen() & 255);
  for (int trial = 0; trial < 2000; ++trial) {
    const int size = 3 + 2 * (gen() % 5);  // 3, 5, ..., 11
    const float dxx = 0.5f + 1.5f * uni(gen), dxy = (uni(gen) - 0.5f);
    const float dyx = (uni(gen) - 0.5f), dyy = 0.5f + 1.5f * uni(gen);
    const float x = 30.0f + 40.0f * uni(gen), y = 25.0f + 30.0f * uni(gen);
    std::vector<float> ref, out(3 * size * size);
    RefSample(img, width, x, y, dxx, dxy, dyx, dyy, size, ref);
    TexKernel::SampleWindow(img.data(), width, x, y, dxx, dxy, dyx, dyy, size, out.data());
    for (size_t i = 0; i < ref.size(); ++i)
      if (std::fabs(ref[i] - out[i]) > sample_tol) { CHECK(false, "sample trial %d value %zu: %.9g vs %.9g", trial, i, ref[i], out[i]); break; }
  }

  // Normalize and dot only differ by the summation order.
  double worst_n = 0, worst_d = 0;
  for (int trial = 0; trial < 2000; ++trial) {
    const int size = 3 + 2 * (gen() % 5);
    const int n = 3 * size * size;
    std::vector<float> a(n), b(n);
    for (int i = 0; i < n; ++i) { a[i] = 255.0f * uni(gen); b[i] = 255.0f * uni(gen); }
    std::vector<float> ra = a, rb = b;
    RefNormalize(ra);  RefNormalize(rb);
    TexKernel::Normalize(a.data(), n);  TexKernel::Normalize(b.data(), n);
    for (int i = 0; i < n; ++i) worst_n = std::fmax(worst_n, std::fabs(ra[i] - a[i]));
    const double rd = RefDot(ra, rb), d = TexKernel::Dot(a.data(), b.data(), n);
    worst_d = std::fmax(worst_d, std::fabs(rd - d));
  }
  std::printf("max |normalize diff| %.3g, max |dot diff| %.3g\n", worst_n, worst_d);
  CHECK(worst_n < 1e-4, "normalize differs by %g", worst_n);
  CHECK(worst_d < 1e-5, "dot differs by %g", worst_d);

  // A constant texture has no deviation: the values become zero.
  std::vector<float> c(147, 100.0f);
  TexKernel::Normalize(c.data(), (int)c.size());
  for (float v : c) CHECK(v == 0.0f, "constant texture not zero");

  std::printf(failures ? "FAILED\n" : "OK\n");
  return failures ? 1 : 0;
}
