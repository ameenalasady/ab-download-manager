#include "downloadpartslots.h"
#include <QSet>
#include <algorithm>

namespace {
bool active(const QVariantMap &part) {
    const auto state = part.value("state").toString();
    return state == "connecting" || state == "downloading" || state == "disconnected";
}
QVariantMap vacant() {
    return {{"id", QString()}, {"state", "idle"}, {"downloadedLabel", QStringLiteral("—")},
            {"sizeLabel", QStringLiteral("—")}, {"progress", -1.0}};
}
}

int DownloadPartSlots::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_slots.size();
}
QVariant DownloadPartSlots::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_slots.size() || role != PartData) return {};
    return m_slots[index.row()];
}
QHash<int, QByteArray> DownloadPartSlots::roleNames() const { return {{PartData, "modelData"}}; }
void DownloadPartSlots::replace(int row, const QVariantMap &part) {
    if (m_slots[row] == part) return;
    m_slots[row] = part;
    emit dataChanged(index(row), index(row), {PartData});
}
void DownloadPartSlots::append(const QVariantMap &part) {
    const int row = m_slots.size();
    beginInsertRows({}, row, row);
    m_slots.append(part);
    endInsertRows();
}
void DownloadPartSlots::update(const QVariantMap &projection, int initialSlots) {
    // A cold cache is not a retirement. Keep cached rows while disconnected.
    if (!projection.contains("available")) return;
    if (!projection.value("available").toBool()) {
        if (m_slots.isEmpty()) return;
        beginRemoveRows({}, 0, m_slots.size() - 1);
        m_slots.clear();
        endRemoveRows();
        return;
    }
    const auto items = projection.value("items").toList();
    QHash<QString, QVariantMap> ranges;
    int activeCount = 0;
    for (const auto &value : items) {
        const auto part = value.toMap();
        ranges.insert(part.value("id").toString(), part);
        if (active(part)) ++activeCount;
    }
    if (m_slots.isEmpty()) {
        // On a cold open show all live work, plus initial idle/finished ranges up
        // to the configured parallelism. Do not seed hundreds of history rows.
        int fillers = std::max(0, std::min(std::clamp(initialSlots, 1, 256), int(items.size())) - activeCount);
        for (const auto &value : items) {
            const auto part = value.toMap();
            if (active(part)) append(part);
            else if (fillers > 0) { append(part); --fillers; }
        }
        return;
    }
    QSet<QString> assigned;
    // Preserve every continuing range in its current slot, regardless of wire
    // ordering. Length/downloaded/state may change even when the offset does not.
    for (int row = 0; row < m_slots.size(); ++row) {
        const auto id = m_slots[row].value("id").toString();
        const auto found = ranges.constFind(id);
        replace(row, found == ranges.cend() ? vacant() : found.value());
        if (found != ranges.cend()) assigned.insert(id);
    }
    // Only new live work needs a slot. Finished ranges that polling never saw
    // active do not grow the table. Retain terminal rows until they are reused.
    for (const auto &value : items) {
        const auto part = value.toMap(); const auto id = part.value("id").toString();
        if (!active(part) || assigned.contains(id)) continue;
        int free = -1;
        for (int row = 0; row < m_slots.size(); ++row)
            if (!active(m_slots[row])) { free = row; break; }
        if (free >= 0) replace(free, part);
        else append(part);
        assigned.insert(id);
    }
}
