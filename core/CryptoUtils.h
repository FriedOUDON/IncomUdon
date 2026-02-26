#pragma once

#include <QObject>
#include <QString>

class CryptoUtils : public QObject
{
    Q_OBJECT

public:
    explicit CryptoUtils(QObject* parent = nullptr);

    Q_INVOKABLE QString sha256Hex(const QString& text) const;
    Q_INVOKABLE bool isSha256Hex(const QString& text) const;
    Q_INVOKABLE QString normalizePasswordHashHex(const QString& text) const;
};

