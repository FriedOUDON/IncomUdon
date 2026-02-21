#pragma once

#include <QObject>
#include <QByteArray>
#include <QMap>
#include <QtGlobal>

struct JitterFrame
{
    quint16 seq = 0;
    QByteArray frame;
};

class JitterBuffer : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int minBufferedFrames
               READ minBufferedFrames
               WRITE setMinBufferedFrames
               NOTIFY minBufferedFramesChanged)

public:
    explicit JitterBuffer(QObject* parent = nullptr);

    int minBufferedFrames() const;
    void setMinBufferedFrames(int frames);

    int size() const;

    void pushFrame(quint16 seq, const QByteArray& frame);
    QByteArray popFrame(bool requireMin = true);
    void clear();

signals:
    void minBufferedFramesChanged();
    void sizeChanged();

private:
    QMap<quint16, QByteArray> m_frames;
    int m_minBufferedFrames = 2;
    bool m_expectedSeqValid = false;
    quint16 m_expectedSeq = 0;
};
