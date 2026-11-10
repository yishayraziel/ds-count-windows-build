#pragma once
// Allocation-free (after prepare) radix-2 complex FFT, double precision.
#include "Common.h"
#include <complex>

namespace dsq
{
class FFT
{
public:
    using Complex = std::complex<double>;

    void prepare (int sizeIn)
    {
        size = sizeIn;
        twiddles.resize ((size_t) size / 2);
        for (int i = 0; i < size / 2; ++i)
            twiddles[(size_t) i] = std::polar (1.0, -2.0 * kPi * i / size);

        reversed.resize ((size_t) size);
        int bits = 0;
        while ((1 << bits) < size)
            ++bits;
        for (int i = 0; i < size; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b))
                    r |= 1 << (bits - 1 - b);
            reversed[(size_t) i] = r;
        }
    }

    int getSize() const noexcept { return size; }

    void forward (Complex* data) const noexcept { transform (data, false); }

    /** Inverse transform, scaled by 1/N. */
    void inverse (Complex* data) const noexcept
    {
        transform (data, true);
        const double scale = 1.0 / size;
        for (int i = 0; i < size; ++i)
            data[i] *= scale;
    }

private:
    void transform (Complex* d, bool inverseTransform) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const int j = reversed[(size_t) i];
            if (j > i)
                std::swap (d[i], d[j]);
        }

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1, step = size / len;
            for (int start = 0; start < size; start += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    const Complex tw = twiddles[(size_t) (k * step)];
                    const double wr = tw.real(), wi = inverseTransform ? -tw.imag() : tw.imag();
                    const Complex& b = d[start + k + half];
                    const double br = b.real() * wr - b.imag() * wi;
                    const double bi = b.real() * wi + b.imag() * wr;
                    const Complex a = d[start + k];
                    d[start + k] = Complex (a.real() + br, a.imag() + bi);
                    d[start + k + half] = Complex (a.real() - br, a.imag() - bi);
                }
            }
        }
    }

    int size = 0;
    std::vector<Complex> twiddles;
    std::vector<int> reversed;
};
} // namespace dsq
