#include "AudioBuffer.h"

AudioBuffer::AudioBuffer(QObject* parent)
    : QObject(parent)
{
}

int AudioBuffer::maxFrames() const
{
    return m_maxFrames;
}

void AudioBuffer::setMaxFrames(int frames)
{
    if (m_maxFrames == frames)
        return;

    m_maxFrames = frames;
    emit maxFramesChanged();
}

int AudioBuffer::size() const
{
    return m_queue.size();
}

void AudioBuffer::push(const QByteArray& frame)
{
    if (m_queue.size() >= m_maxFrames)
        m_queue.dequeue();

    m_queue.enqueue(frame);
    emit sizeChanged();
}

QByteArray AudioBuffer::pop()
{
    if (m_queue.isEmpty())
        return {};

    QByteArray frame = m_queue.dequeue();
    emit sizeChanged();
    return frame;
}
