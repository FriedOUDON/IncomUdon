#include "JitterBuffer.h"

namespace {
static int seqForwardDistance(quint16 from, quint16 to)
{
    return static_cast<int>((static_cast<quint32>(to) - static_cast<quint32>(from)) & 0xFFFFu);
}
}

JitterBuffer::JitterBuffer(QObject* parent)
    : QObject(parent)
{
}

int JitterBuffer::minBufferedFrames() const
{
    return m_minBufferedFrames;
}

void JitterBuffer::setMinBufferedFrames(int frames)
{
    if (m_minBufferedFrames == frames)
        return;

    m_minBufferedFrames = frames;
    emit minBufferedFramesChanged();
}

int JitterBuffer::size() const
{
    return m_frames.size();
}

void JitterBuffer::pushFrame(quint16 seq, const QByteArray& frame)
{
    if (frame.isEmpty())
        return;

    if (m_expectedSeqValid)
    {
        const int behind = seqForwardDistance(seq, m_expectedSeq);
        if (behind > 0 && behind < 32768)
            return;
    }

    if (m_frames.contains(seq))
        return;

    m_frames.insert(seq, frame);
    if (!m_expectedSeqValid)
    {
        m_expectedSeq = seq;
        m_expectedSeqValid = true;
    }
    emit sizeChanged();
}

QByteArray JitterBuffer::popFrame(bool requireMin)
{
    if (requireMin && m_frames.size() < m_minBufferedFrames)
        return {};

    if (m_frames.isEmpty())
        return {};

    if (!m_expectedSeqValid)
    {
        m_expectedSeq = m_frames.firstKey();
        m_expectedSeqValid = true;
    }

    if (m_frames.contains(m_expectedSeq))
    {
        const QByteArray out = m_frames.take(m_expectedSeq);
        m_expectedSeq = static_cast<quint16>(m_expectedSeq + 1);
        emit sizeChanged();
        return out;
    }

    quint16 nearestSeq = 0;
    int nearestDist = 0x7FFFFFFF;
    for (auto it = m_frames.constBegin(); it != m_frames.constEnd(); ++it)
    {
        const int d = seqForwardDistance(m_expectedSeq, it.key());
        if (d < nearestDist)
        {
            nearestDist = d;
            nearestSeq = it.key();
        }
    }

    const int waitWindow = qMax(1, qMin(2, m_minBufferedFrames / 3));
    const bool shouldWait = requireMin &&
                            (nearestDist <= waitWindow) &&
                            (m_frames.size() < (m_minBufferedFrames + waitWindow));
    if (shouldWait)
        return {};

    m_expectedSeq = nearestSeq;
    const QByteArray out = m_frames.take(m_expectedSeq);
    m_expectedSeq = static_cast<quint16>(m_expectedSeq + 1);
    emit sizeChanged();
    return out;
}

void JitterBuffer::clear()
{
    if (m_frames.isEmpty() && !m_expectedSeqValid)
        return;

    m_frames.clear();
    m_expectedSeqValid = false;
    m_expectedSeq = 0;
    emit sizeChanged();
}
