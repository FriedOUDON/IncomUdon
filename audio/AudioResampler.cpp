#include "AudioResampler.h"

void AudioResampler::setRates(int inRate, int outRate)
{
    if (inRate <= 0 || outRate <= 0)
        return;

    if (m_inRate == inRate && m_outRate == outRate)
        return;

    m_inRate = inRate;
    m_outRate = outRate;
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

    m_cache.append(input);

    const double step = static_cast<double>(m_inRate) /
                        static_cast<double>(m_outRate);

    while (m_pos + 1.0 < m_cache.size())
    {
        const int idx = static_cast<int>(m_pos);
        const double frac = m_pos - static_cast<double>(idx);
        const double s0 = static_cast<double>(m_cache[idx]);
        const double s1 = static_cast<double>(m_cache[idx + 1]);
        const double v = s0 + (s1 - s0) * frac;

        int sample = qRound(v);
        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;

        output.append(static_cast<qint16>(sample));
        m_pos += step;
    }

    int drop = static_cast<int>(m_pos);
    if (drop > 0)
    {
        if (drop > m_cache.size())
            drop = m_cache.size();
        m_cache.remove(0, drop);
        m_pos -= static_cast<double>(drop);
        if (m_pos < 0.0)
            m_pos = 0.0;
    }
}
