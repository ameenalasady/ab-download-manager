#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Presentation slots, NOT engine worker IDs. Range IDs are exact decimal offsets;
// dynamic splitting creates new ranges, while the displayed row can be reused.
// A reused row keeps counting: its byte labels add up every finished range it held
// before, so Downloaded/Total never fall back to zero when it starts a new part.
// The data itself (offset, length, downloaded) stays the exact range, and the raw
// download.parts projection remains the source for the complete piece bar.
class DownloadPartSlots : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
public:
    enum Role { PartData = Qt::UserRole + 1 };
    explicit DownloadPartSlots(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void update(const QVariantMap &projection, int initialSlots, const QString &sizeUnit = QStringLiteral("BinaryBytes"));
private:
    struct Slot {
        QVariantMap range;
        qint64 carriedDone = 0; // Bytes of the finished ranges this row held before `range`.
        qint64 carriedSize = 0;
    };
    QVariantMap presented(const Slot &slot) const;
    void replace(int row, const Slot &slot);
    void append(const Slot &slot);
    QList<Slot> m_slots;
    QString m_sizeUnit;
};
