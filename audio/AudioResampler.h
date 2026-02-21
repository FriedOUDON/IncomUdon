#pragma once

#include <QVector>
#include <QtGlobal>

class AudioResampler
{
public:
    void setRates(int inRate, int outRate);
    void reset();
    void push(const QVector<qint16>& input, QVector<qint16>& output);

private:
    int m_inRate = 8000;
    int m_outRate = 8000;
    double m_pos = 0.0;
    QVector<qint16> m_cache;
};
