#ifndef PMVS3_TEX_KERNEL_H
#define PMVS3_TEX_KERNEL_H

// Kernels for the texture windows of the patch optimization. With
// PMVS_USE_HIGHWAY they are written with Google Highway, using 128-bit
// vectors (4 floats) so that the results do not depend on the CPU.

namespace PMVS3 {
namespace TexKernel {

// Bilinear samples of a size x size window of an 8-bit RGB image into out
// (3 * size * size floats, rgb interleaved, row by row). The sample
// positions are generated incrementally: row r starts at
// (x + r * dyx, y + r * dyy), and every sample adds (dxx, dxy) to the previous
// one. The whole window including a margin of one pixel must lie inside the
// image. The result is identical to calling Cimage::getColor() for each pixel.
void SampleWindow(const unsigned char* image, int width, float x, float y,
                  float dxx, float dxy, float dyx, float dyy, int size,
                  float* out);

// Subtracts the mean of each color channel and divides by the standard
// deviation (of all values). n is the number of floats (3 * pixels).
void Normalize(float* tex, int n);

// Sum of the products divided by n.
float Dot(const float* a, const float* b, int n);

}  // namespace TexKernel
}  // namespace PMVS3

#endif  // PMVS3_TEX_KERNEL_H
