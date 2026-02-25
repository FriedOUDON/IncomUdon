#include "LicenseProvider.h"

#include <QFile>
#include <QStringConverter>
#include <QTextStream>

LicenseProvider::LicenseProvider(QObject* parent)
    : QObject(parent)
{
}

QString LicenseProvider::readFirstAvailable(const QStringList& paths, const QString& title) const
{
    for (const QString& path : paths)
    {
        QFile file(path);
        if (!file.exists())
            continue;
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        return in.readAll();
    }

    return QStringLiteral("Failed to read %1").arg(title);
}

QString LicenseProvider::combinedLicenses() const
{
    const QString appLicense = readFirstAvailable(
        {
            QStringLiteral(":/qt/qml/IncomUdon/LICENSE"),
            QStringLiteral(":/LICENSE")
        },
        QStringLiteral("LICENSE"));

    const QString notices = readFirstAvailable(
        {
            QStringLiteral(":/qt/qml/IncomUdon/THIRD_PARTY_NOTICES.md"),
            QStringLiteral(":/THIRD_PARTY_NOTICES.md")
        },
        QStringLiteral("THIRD_PARTY_NOTICES.md"));

    const QString apache = readFirstAvailable(
        {
            QStringLiteral(":/qt/qml/IncomUdon/LICENSES/Apache-2.0.txt"),
            QStringLiteral(":/LICENSES/Apache-2.0.txt")
        },
        QStringLiteral("LICENSES/Apache-2.0.txt"));

    const QString lgpl = readFirstAvailable(
        {
            QStringLiteral(":/qt/qml/IncomUdon/LICENSES/LGPL-2.1.txt"),
            QStringLiteral(":/LICENSES/LGPL-2.1.txt")
        },
        QStringLiteral("LICENSES/LGPL-2.1.txt"));

    return QStringLiteral("=== App License (LICENSE) ===\n%1\n\n"
                          "=== Third Party Notices ===\n%2\n\n"
                          "=== Apache-2.0 ===\n%3\n\n"
                          "=== LGPL-2.1 ===\n%4")
        .arg(appLicense, notices, apache, lgpl);
}
