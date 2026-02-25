#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class LicenseProvider : public QObject
{
    Q_OBJECT

public:
    explicit LicenseProvider(QObject* parent = nullptr);

    Q_INVOKABLE QString combinedLicenses() const;

private:
    QString readFirstAvailable(const QStringList& paths, const QString& title) const;
};

