#include "Fec.h"

#include <QtEndian>

namespace {
static bool gf_ready = false;
static quint8 gf_exp[512];
static quint8 gf_log[256];

static void gf_init()
{
    if (gf_ready)
        return;

    int x = 1;
    for (int i = 0; i < 255; ++i)
    {
        gf_exp[i] = static_cast<quint8>(x);
        gf_log[static_cast<quint8>(x)] = static_cast<quint8>(i);
        x <<= 1;
        if (x & 0x100)
            x ^= 0x11d;
    }
    for (int i = 255; i < 512; ++i)
        gf_exp[i] = gf_exp[i - 255];

    gf_log[0] = 0;
    gf_ready = true;
}

static inline quint8 gf_mul(quint8 a, quint8 b)
{
    if (a == 0 || b == 0)
        return 0;
    const int logSum = gf_log[a] + gf_log[b];
    return gf_exp[logSum];
}

static inline quint8 gf_div(quint8 a, quint8 b)
{
    if (a == 0)
        return 0;
    if (b == 0)
        return 0;
    int diff = gf_log[a] - gf_log[b];
    if (diff < 0)
        diff += 255;
    return gf_exp[diff];
}

static inline quint8 gf_pow2(int exp)
{
    exp %= 255;
    if (exp < 0)
        exp += 255;
    return gf_exp[exp];
}

static void xorBytes(QByteArray& dst, const QByteArray& src)
{
    const int len = qMin(dst.size(), src.size());
    char* d = dst.data();
    const char* s = src.constData();
    for (int i = 0; i < len; ++i)
        d[i] ^= s[i];
}

static void xorMulBytes(QByteArray& dst, const QByteArray& src, quint8 factor)
{
    const int len = qMin(dst.size(), src.size());
    char* d = dst.data();
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src.constData());
    for (int i = 0; i < len; ++i)
    {
        const quint8 v = gf_mul(s[i], factor);
        d[i] = static_cast<char>(static_cast<quint8>(d[i]) ^ v);
    }
}
} // namespace

void FecEncoder::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    reset();
}

bool FecEncoder::enabled() const
{
    return m_enabled;
}

void FecEncoder::reset()
{
    m_frameSize = 0;
    m_blockStart = 0;
    m_inBlock = 0;
    m_parityP.clear();
    m_parityQ.clear();
}

void FecEncoder::setBlockSize(int blockSize)
{
    if (blockSize <= 0 || m_blockSize == blockSize)
        return;
    m_blockSize = blockSize;
    reset();
}

int FecEncoder::blockSize() const
{
    return m_blockSize;
}

void FecEncoder::beginBlock(quint16 blockStart, int frameSize)
{
    m_blockStart = blockStart;
    m_inBlock = 0;
    m_frameSize = frameSize;
    m_parityP = QByteArray(m_frameSize, 0);
    m_parityQ = QByteArray(m_frameSize, 0);
}

QVector<FecParityPacket> FecEncoder::addFrame(quint16 audioSeq,
                                             const QByteArray& frame)
{
    QVector<FecParityPacket> out;
    if (!m_enabled || frame.isEmpty() || m_blockSize <= 0)
        return out;

    gf_init();

    const int frameSize = frame.size();
    const int index = m_blockSize > 0 ? (audioSeq % m_blockSize) : 0;
    const quint16 blockStart = static_cast<quint16>(audioSeq - index);

    if (m_inBlock == 0 || frameSize != m_frameSize || blockStart != m_blockStart)
        beginBlock(blockStart, frameSize);

    xorBytes(m_parityP, frame);
    xorMulBytes(m_parityQ, frame, gf_pow2(index));

    m_inBlock++;
    if (m_inBlock >= m_blockSize)
    {
        FecParityPacket p;
        p.blockStart = m_blockStart;
        p.blockSize = static_cast<quint8>(m_blockSize);
        p.parityIndex = 0;
        p.data = m_parityP;
        out.append(p);

        FecParityPacket q;
        q.blockStart = m_blockStart;
        q.blockSize = static_cast<quint8>(m_blockSize);
        q.parityIndex = 1;
        q.data = m_parityQ;
        out.append(q);

        m_inBlock = 0;
        m_parityP.clear();
        m_parityQ.clear();
    }

    return out;
}

void FecDecoder::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    reset();
}

bool FecDecoder::enabled() const
{
    return m_enabled;
}

void FecDecoder::reset()
{
    m_blocks.clear();
    m_hasNextBlock = false;
    m_nextBlockStart = 0;
}

void FecDecoder::setBlockSize(int blockSize)
{
    if (blockSize <= 0 || m_blockSize == blockSize)
        return;
    m_blockSize = blockSize;
    reset();
}

int FecDecoder::blockSize() const
{
    return m_blockSize;
}

FecDecoder::Block* FecDecoder::ensureBlock(quint16 blockStart, int frameSize)
{
    if (m_blocks.contains(blockStart))
    {
        Block& block = m_blocks[blockStart];
        if (block.frameSize != frameSize && frameSize > 0)
        {
            m_blocks.remove(blockStart);
        }
    }

    if (!m_blocks.contains(blockStart))
    {
        Block block;
        block.start = blockStart;
        block.blockSize = m_blockSize;
        block.frameSize = frameSize;
        block.data.resize(m_blockSize);
        block.present.fill(false, m_blockSize);
        block.parityPresent[0] = false;
        block.parityPresent[1] = false;
        m_blocks.insert(blockStart, block);
    }

    Block& block = m_blocks[blockStart];
    if (frameSize > 0 && block.frameSize == 0)
        block.frameSize = frameSize;
    return &block;
}

bool FecDecoder::canRecover(const Block& block, int* missingCount, QVector<int>* missingIdx) const
{
    int missing = 0;
    QVector<int> missingIndexes;
    for (int i = 0; i < block.blockSize; ++i)
    {
        if (!block.present.value(i))
        {
            missing++;
            missingIndexes.append(i);
        }
    }

    if (missingCount)
        *missingCount = missing;
    if (missingIdx)
        *missingIdx = missingIndexes;

    if (missing == 0)
        return true;
    if (missing == 1)
        return block.parityPresent[0] || block.parityPresent[1];
    if (missing == 2)
        return block.parityPresent[0] && block.parityPresent[1];
    return false;
}

QVector<FecDecodedFrame> FecDecoder::outputBlock(Block& block, bool force)
{
    QVector<FecDecodedFrame> out;
    if (block.blockSize <= 0)
        return out;

    gf_init();

    int missingCount = 0;
    QVector<int> missingIdx;
    const bool recoverable = canRecover(block, &missingCount, &missingIdx);
    if (!recoverable && !force)
        return out;

    const int frameSize = block.frameSize;
    if (frameSize <= 0)
    {
        return out;
    }

    if (missingCount == 0)
    {
        return out;
    }

    const QVector<int> missingBefore = missingIdx;

    if ((missingCount == 1 || missingCount == 2) && recoverable)
    {
        QByteArray sumP(frameSize, 0);
        QByteArray sumQ(frameSize, 0);
        for (int i = 0; i < block.blockSize; ++i)
        {
            if (!block.present[i])
                continue;
            xorBytes(sumP, block.data[i]);
            xorMulBytes(sumQ, block.data[i], gf_pow2(i));
        }

        if (missingCount == 1)
        {
            const int mi = missingIdx[0];
            QByteArray recovered(frameSize, 0);
            if (block.parityPresent[0])
            {
                recovered = block.parity[0];
                xorBytes(recovered, sumP);
            }
            else if (block.parityPresent[1])
            {
                recovered = block.parity[1];
                xorBytes(recovered, sumQ);
                const quint8 coef = gf_pow2(mi);
                for (int b = 0; b < frameSize; ++b)
                {
                    recovered[b] = static_cast<char>(
                        gf_div(static_cast<quint8>(recovered[b]), coef));
                }
            }
            block.data[mi] = recovered;
            block.present[mi] = true;
        }
        else if (missingCount == 2)
        {
            const int mi = missingIdx[0];
            const int mj = missingIdx[1];
            QByteArray s = block.parity[0];
            xorBytes(s, sumP);

            QByteArray t = block.parity[1];
            xorBytes(t, sumQ);

            const quint8 gi = gf_pow2(mi);
            const quint8 gj = gf_pow2(mj);
            const quint8 denom = static_cast<quint8>(gi ^ gj);
            if (denom != 0)
            {
                QByteArray di(frameSize, 0);
                for (int b = 0; b < frameSize; ++b)
                {
                    const quint8 tb = static_cast<quint8>(t[b]);
                    const quint8 sb = static_cast<quint8>(s[b]);
                    const quint8 numerator = static_cast<quint8>(tb ^ gf_mul(sb, gj));
                    di[b] = static_cast<char>(gf_div(numerator, denom));
                }
                QByteArray dj = di;
                xorBytes(dj, s);

                block.data[mi] = di;
                block.data[mj] = dj;
                block.present[mi] = true;
                block.present[mj] = true;
            }
        }
    }

    for (int i = 0; i < missingBefore.size(); ++i)
    {
        const int idx = missingBefore.at(i);
        if (idx < 0 || idx >= block.blockSize)
            continue;
        if (!block.present[idx] && !force)
            continue;

        FecDecodedFrame frame;
        frame.seq = static_cast<quint16>(block.start + idx);
        frame.frame = block.data[idx];
        if (frame.frame.isEmpty() && !force)
            continue;
        out.append(frame);
    }

    return out;
}

QVector<FecDecodedFrame> FecDecoder::tryOutput(bool force)
{
    QVector<FecDecodedFrame> out;
    if (m_blocks.isEmpty())
        return out;

    QVector<quint16> completed;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it)
    {
        Block& block = it.value();
        int missingCount = 0;
        const bool recoverable = canRecover(block, &missingCount, nullptr);
        if (recoverable && missingCount > 0)
            out.append(outputBlock(block, force));

        int afterMissing = 0;
        canRecover(block, &afterMissing, nullptr);
        if (afterMissing == 0)
            completed.append(it.key());
    }

    for (quint16 key : completed)
        m_blocks.remove(key);

    while (m_blocks.size() > 24)
        m_blocks.erase(m_blocks.begin());

    if (m_blocks.isEmpty())
    {
        m_hasNextBlock = false;
        m_nextBlockStart = 0;
    }

    return out;
}

QVector<FecDecodedFrame> FecDecoder::pushData(quint16 audioSeq,
                                              const QByteArray& frame)
{
    QVector<FecDecodedFrame> out;
    if (!m_enabled)
    {
        FecDecodedFrame f;
        f.seq = audioSeq;
        f.frame = frame;
        out.append(f);
        return out;
    }

    if (frame.isEmpty() || m_blockSize <= 0)
        return out;

    const int index = audioSeq % m_blockSize;
    const quint16 blockStart = static_cast<quint16>(audioSeq - index);

    Block* block = ensureBlock(blockStart, frame.size());
    if (!block)
        return out;

    if (index >= 0 && index < block->data.size())
    {
        block->data[index] = frame;
        block->present[index] = true;
    }

    return tryOutput(false);
}

QVector<FecDecodedFrame> FecDecoder::pushParity(quint16 blockStart,
                                                quint8 blockSize,
                                                quint8 parityIndex,
                                                const QByteArray& data)
{
    QVector<FecDecodedFrame> out;
    if (!m_enabled)
        return out;

    if (blockSize != static_cast<quint8>(m_blockSize))
        return out;

    if (parityIndex > 1)
        return out;

    Block* block = ensureBlock(blockStart, data.size());
    if (!block)
        return out;

    if (block->frameSize != data.size())
        block->frameSize = data.size();

    block->parity[parityIndex] = data;
    block->parityPresent[parityIndex] = true;

    return tryOutput(false);
}
