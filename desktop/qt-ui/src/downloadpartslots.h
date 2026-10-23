#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Presentation slots, NOT engine worker IDs. Range IDs are exact decimal offsets;
// dynamic splitting creates new ranges, while the displayed row can be reused.
// The raw download.parts projection remains the source for the complete piece bar.
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
    Q_INVOKABLE void update(const QVariantMap &projection, int initialSlots);
private:
    void replace(int row, const QVariantMap &part);
    void append(const QVariantMap &part);
    QList<QVariantMap> m_slots;
};
