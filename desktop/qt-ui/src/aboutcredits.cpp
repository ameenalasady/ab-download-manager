#include "aboutcredits.h"
#include <QCollator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <algorithm>

namespace {
QByteArray resource(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(1024 * 1024) : QByteArray();
}
}

QVariantList AboutCredits::translators() const {
    const auto document = QJsonDocument::fromJson(resource(QStringLiteral(":/qt-ui/credits/translators.json")));
    QList<QPair<QString, QStringList>> languages;
    const auto object = document.object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        QStringList names;
        for (const auto &entry : it.value().toArray()) {
            const auto name = entry.toObject().value("name").toString().trimmed();
            if (!name.isEmpty() && name.size() <= 200) names.append(name);
        }
        if (names.isEmpty()) continue;
        const QLocale locale(it.key());
        // Native name first, so speakers find their language; English name for everyone else.
        // Codes Qt does not know (it falls back to the C locale) are shown as they are.
        const bool known = locale.language() != QLocale::C;
        const QString native = known ? locale.nativeLanguageName() : it.key();
        const QString english = known ? QLocale::languageToString(locale.language()) : QString();
        QString territory;
        if (known && (it.key().contains('-') || it.key().contains('_')) && !native.contains(locale.nativeTerritoryName())) territory = locale.nativeTerritoryName();
        QString label = native.isEmpty() ? it.key() : native;
        if (!territory.isEmpty()) label += QStringLiteral(" (%1)").arg(territory);
        if (!english.isEmpty() && english.compare(native, Qt::CaseInsensitive) != 0) label += QStringLiteral(" — %1").arg(english);
        languages.append({label, names});
    }
    QCollator collator; collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(languages.begin(), languages.end(), [&](const auto &a, const auto &b) { return collator.compare(a.first, b.first) < 0; });
    QVariantList result;
    for (const auto &language : std::as_const(languages))
        result.append(QVariantMap{{"language", language.first}, {"names", language.second}});
    return result;
}

QString AboutCredits::licenseText() const {
    return QString::fromUtf8(resource(QStringLiteral(":/qt-ui/credits/LICENSE")));
}
