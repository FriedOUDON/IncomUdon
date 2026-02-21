#pragma once

#include <QObject>
#include <QByteArray>
#include <QQueue>

class AudioBuffer : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int maxFrames
               READ maxFrames
               WRITE setMaxFrames
               NOTIFY maxFramesChanged)

public:
    explicit AudioBuffer(QObject* parent = nullptr);

    int maxFrames() const;
    void setMaxFrames(int frames);

    int size() const;

    void push(const QByteArray& frame);
    QByteArray pop();

signals:
    void maxFramesChanged();
    void sizeChanged();

private:
    QQueue<QByteArray> m_queue;
    int m_maxFrames = 50;
};
