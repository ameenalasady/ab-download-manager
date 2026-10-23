#pragma once
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Static About data bundled with the frontend: the original project's translator credits
// (shared/resources credits/translators.json) and its license text. No I/O beyond resources.
class AboutCredits : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // One entry per language, sorted by display name: {language, names}. Malformed or
    // missing data yields an empty list.
    Q_INVOKABLE QVariantList translators() const;
    Q_INVOKABLE QString licenseText() const;
};
