#pragma once

#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QtGlobal>

struct FecParityPacket
{
    quint16 blockStart = 0;
    quint8 blockSize = 0;
    quint8 parityIndex = 0;
    QByteArray data;
};

struct FecDecodedFrame
{
    quint16 seq = 0;
    QByteArray frame;
};

class FecEncoder
{
public:
    void setEnabled(bool enabled);
    bool enabled() const;

    void reset();
    void setBlockSize(int blockSize);
    int blockSize() const;

    QVector<FecParityPacket> addFrame(quint16 audioSeq,
                                      const QByteArray& frame);

private:
    void beginBlock(quint16 blockStart, int frameSize);

    bool m_enabled = false;
    int m_blockSize = 6;
    int m_frameSize = 0;
    quint16 m_blockStart = 0;
    int m_inBlock = 0;
    QByteArray m_parityP;
    QByteArray m_parityQ;
};

class FecDecoder
{
public:
    void setEnabled(bool enabled);
    bool enabled() const;

    void reset();
    void setBlockSize(int blockSize);
    int blockSize() const;

    QVector<FecDecodedFrame> pushData(quint16 audioSeq,
                                      const QByteArray& frame);
    QVector<FecDecodedFrame> pushParity(quint16 blockStart,
                                        quint8 blockSize,
                                        quint8 parityIndex,
                                        const QByteArray& data);

private:
    struct Block
    {
        quint16 start = 0;
        int blockSize = 0;
        int frameSize = 0;
        QVector<QByteArray> data;
        QVector<bool> present;
        QByteArray parity[2];
        bool parityPresent[2] = {false, false};
    };

    Block* ensureBlock(quint16 blockStart, int frameSize);
    bool canRecover(const Block& block, int* missingCount, QVector<int>* missingIdx) const;
    QVector<FecDecodedFrame> outputBlock(Block& block, bool force);
    QVector<FecDecodedFrame> tryOutput(bool force);

    bool m_enabled = false;
    int m_blockSize = 6;
    bool m_hasNextBlock = false;
    quint16 m_nextBlockStart = 0;
    QMap<quint16, Block> m_blocks;
};
