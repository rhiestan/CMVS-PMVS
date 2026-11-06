# Performance optimizations (`perfopt` branch)

This fork of [CMVS-PMVS](https://github.com/pmoulon/CMVS-PMVS) adds a modern
build, performance optimizations for `pmvs2` and a fix for non-deterministic
results. The work lives on the `perfopt` branch (the default branch of this
fork); `master` tracks the upstream repository, so it can be synchronized with
it.

Timings below were measured on a 32-thread Windows machine (Clang 22) with a
data set of 11 images of 1416x1064 pixels.

## Build and CI

- CMake presets (`windows-clang`, `linux-clang`, `macos-clang`) with Clang,
  Ninja and vcpkg, in `program/`. vcpkg provides libjpeg-turbo, mimalloc and
  Google Highway; the other libraries (Eigen, NLopt, Graclus, miniBoost,
  tinycthread, CImg) stay bundled.
- Portability fixes for current compilers: a `strings.h` shim for the bundled
  Metis on Windows, missing includes and feature checks in NLopt, a
  permutation sort instead of proxy iterators in `stann` (libc++ rejects
  them), no strict POSIX feature macros on macOS, and no Eigen vectorization
  (the bundled Eigen 3.2 does not compile with its SSE code on current Clang).
- GitHub Actions builds `pmvs2`, `cmvs` and `genOption` on Windows, Linux and
  macOS and runs a unit test. Pushing a tag packages the tools and publishes a
  GitHub release.

## Optimizations (`pmvs2`)

| Threads | Original | Now |
|---|---|---|
| 1 | 41.9 s | 30.6 s |
| 16 | 8.2 s | 5.2 s |

- **Highway kernels** (`program/base/pmvs/tex_kernel.*`) sample a whole
  texture window in one call instead of calling `getColor()` per texel, and
  normalize it and compute the dot products. They use 128-bit vectors, so the
  results do not depend on the CPU. Sampling is bit-identical to the old code;
  the sums in normalize and dot differ in the last bits. Without Highway a plain
  C++ fallback is used. A unit test compares the kernels with the code they
  replace.
- **mimalloc** for all C++ allocations (about 7% on its own).
- `grabTex` no longer clears and zero-fills the texture buffers on every call.
- `filterExact` sets the reference images of the patches in parallel; the grids
  are updated afterwards in the original order.
- A `std::unique_lock` replaces the `std::deque<std::lock_guard>` that was used
  to lock conditionally, which allocated on every use.
- `writePLY` formats the lines with `snprintf` (same output).
- Compiled with `-fno-math-errno`.

`cmvs` builds and starts, but was not profiled.

## Fix: deterministic candidate order

`Cseed::collectCandidates` sorted a vector of `boost::shared_ptr<Cpoint>` with
the default `operator<`, which compares the *addresses*. The intended order
("from the closest") therefore depended on the heap layout, and results varied
from run to run even with one thread. The candidates are now sorted by their
response with a stable sort. With `CPU 1` the results are deterministic; with
several threads they still vary slightly, because the threads race on shared
points, as in the original.
