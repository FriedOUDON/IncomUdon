#include "AudioResampler.h"

#include <QtMath>

namespace
{
constexpr double kPi = 3.14159265358979323846;

double blackmanWindow(int tapIndex, int tapCount)
{
    if (tapCount <= 1)
        return 1.0;

    const double phase = (2.0 * kPi * static_cast<double>(tapIndex)) /
                         static_cast<double>(tapCount - 1);
    return 0.42 - 0.5 * qCos(phase) + 0.08 * qCos(2.0 * phase);
}
}

void AudioResampler::setRates(int inRate, int outRate)
{
    if (inRate <= 0 || outRate <= 0)
        return;

    if (m_inRate == inRate && m_outRate == outRate)
        return;

    m_inRate = inRate;
    m_outRate = outRate;
    m_cutoff = qMin(1.0, static_cast<double>(m_outRate) /
                             static_cast<double>(m_inRate));
    reset();
}

void AudioResampler::reset()
{
    m_cache.clear();
    m_pos = 0.0;
}

void AudioResampler::push(const QVector<qint16>& input, QVector<qint16>& output)
{
    if (input.isEmpty())
        return;

    if (m_inRate == m_outRate)
    {
        output += input;
        return;
    }

    m_cache += input;

    const double step = static_cast<double>(m_inRate) /
                        static_cast<double>(m_outRate);

    // Keep enough look-ahead for the FIR kernel so downsampling can apply a
    // proper low-pass filter instead of interpolating with no anti-aliasing.
    while (m_pos + static_cast<double>(m_halfTaps) < static_cast<double>(m_cache.size()))
    {
        const int sample = qBound(-32768, qRound(sampleAt(m_pos)), 32767);
        output.append(static_cast<qint16>(sample));
        m_pos += step;
    }

    const int keepFrom = qMax(0, static_cast<int>(m_pos) - m_halfTaps);
    if (keepFrom > 0)
    {
        m_cache.remove(0, keepFrom);
        m_pos -= static_cast<double>(keepFrom);
        if (m_pos < 0.0)
            m_pos = 0.0;
    }
}

double AudioResampler::sampleAt(double position) const
{
    if (m_cache.isEmpty())
        return 0.0;

    const int center = static_cast<int>(position);
    const double frac = position - static_cast<double>(center);

    double sum = 0.0;
    double norm = 0.0;
    for (int tap = 0; tap < m_tapCount; ++tap)
    {
        const int relative = tap - (m_halfTaps - 1);
        int srcIndex = center + relative;
        if (srcIndex < 0)
            srcIndex = 0;
        else if (srcIndex >= m_cache.size())
            srcIndex = m_cache.size() - 1;

        const double distance = static_cast<double>(relative) - frac;
        const double coeff = m_cutoff *
                             sinc(m_cutoff * distance) *
                             blackmanWindow(tap, m_tapCount);
        sum += static_cast<double>(m_cache[srcIndex]) * coeff;
        norm += coeff;
    }

    if (qFuzzyIsNull(norm))
        return 0.0;
    return sum / norm;
}

double AudioResampler::sinc(double x)
{
    if (qAbs(x) < 1.0e-9)
        return 1.0;
    const double pix = kPi * x;
    return qSin(pix) / pix;
}
