#include "downloadpartslots.h"
#include "servicedownloads.h"
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
bool finished(const QVariantMap &part) {
    return !part.value("id").toString().isEmpty() && part.value("state").toString() == "completed";
}
}

int DownloadPartSlots::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_slots.size();
}
QVariant DownloadPartSlots::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_slots.size() || role != PartData) return {};
    return presented(m_slots[index.row()]);
}
QHash<int, QByteArray> DownloadPartSlots::roleNames() const { return {{PartData, "modelData"}}; }
QVariantMap DownloadPartSlots::presented(const Slot &slot) const {
    if (slot.carriedDone == 0 && slot.carriedSize == 0) return slot.range;
    auto part = slot.range;
    const auto length = part.value("length");
    part["downloadedLabel"] = ServiceDownloads::formatSize(slot.carriedDone + part.value("downloaded").toString().toLongLong(), m_sizeUnit);
    if (!length.isNull()) part["sizeLabel"] = ServiceDownloads::formatSize(slot.carriedSize + length.toString().toLongLong(), m_sizeUnit);
    return part;
}
void DownloadPartSlots::replace(int row, const Slot &slot) {
    if (m_slots[row].range == slot.range && m_slots[row].carriedDone == slot.carriedDone
        && m_slots[row].carriedSize == slot.carriedSize) return;
    m_slots[row] = slot;
    emit dataChanged(index(row), index(row), {PartData});
}
void DownloadPartSlots::append(const Slot &slot) {
    const int row = m_slots.size();
    beginInsertRows({}, row, row);
    m_slots.append(slot);
    endInsertRows();
}
void DownloadPartSlots::update(const QVariantMap &projection, int initialSlots, const QString &sizeUnit) {
    // A cold cache is not a retirement. Keep cached rows while disconnected.
    if (!projection.contains("available")) return;
    if (sizeUnit != m_sizeUnit) {
        m_sizeUnit = sizeUnit;
        if (!m_slots.isEmpty()) emit dataChanged(index(0), index(m_slots.size() - 1), {PartData});
    }
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
            if (active(part)) append({part});
            else if (fillers > 0) { append({part}); --fillers; }
        }
        return;
    }
    QSet<QString> assigned;
    // Preserve every continuing range in its current slot, regardless of wire
    // ordering. Length/downloaded/state may change even when the offset does not.
    for (int row = 0; row < m_slots.size(); ++row) {
        const auto id = m_slots[row].range.value("id").toString();
        const auto found = ranges.constFind(id);
        // A range that left the projection empties the row and its running total.
        if (found == ranges.cend()) replace(row, {vacant()});
        else replace(row, {found.value(), m_slots[row].carriedDone, m_slots[row].carriedSize});
        if (found != ranges.cend()) assigned.insert(id);
    }
    // Only new live work needs a slot. Finished ranges that polling never saw
    // active do not grow the table. Retain terminal rows until they are reused.
    for (const auto &value : items) {
        const auto part = value.toMap(); const auto id = part.value("id").toString();
        if (!active(part) || assigned.contains(id)) continue;
        int free = -1;
        for (int row = 0; row < m_slots.size(); ++row)
            if (!active(m_slots[row].range)) { free = row; break; }
        if (free >= 0) {
            // The row keeps counting past a part it finished; an idle or empty one adds nothing.
            Slot slot{part, m_slots[free].carriedDone, m_slots[free].carriedSize};
            const auto &previous = m_slots[free].range;
            if (finished(previous)) {
                const auto done = previous.value("downloaded").toString().toLongLong();
                slot.carriedDone += done;
                slot.carriedSize += previous.value("length").isNull() ? done : previous.value("length").toString().toLongLong();
            }
            replace(free, slot);
        } else append({part});
        assigned.insert(id);
    }
}
