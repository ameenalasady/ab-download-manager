#include "previewdownloads.h"
#include "servicedownloads.h"

#include <QUrl>
#include <algorithm>

void qml_register_types_Abdm_Desktop();

// Explicit reference also keeps the QML resources linked from the static library.
static void initializePreviewResources()
{
    Q_INIT_RESOURCE(preview_qml);
    static const bool registered = [] { qml_register_types_Abdm_Desktop(); return true; }();
    Q_UNUSED(registered);
}

PreviewDownloads::PreviewDownloads(QObject *parent) : QAbstractListModel(parent)
{
    initializePreviewResources();
    m_rows = {
        {"preview-1", "Plasma-workspace.tar.zst", "downloading", 0.64,
         "412 MiB / 644 MiB", "Example: 4.2 MiB/s · 55 seconds remaining", "package-x-generic"},
        {"preview-2", "Design-reference.pdf", "paused", 0.28,
         "8.4 MiB / 30 MiB", "Paused · example transfer", "application-pdf"},
        {"preview-3", "Landscape-wallpapers.zip", "queued", 0.0,
         "0 B / 128 MiB", "Waiting · example queue", "application-x-archive"},
        {"preview-4", "Getting-started.webm", "completed", 1.0,
         "82 MiB", "Complete · example only; no file exists", "video-x-generic"},
        {"preview-5", "Release-checksums.txt", "failed", 0.0,
         "0 B", "Example error: connection interrupted", "text-x-generic"}
    };
    rebuild();
}

int PreviewDownloads::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QHash<int, QByteArray> PreviewDownloads::roleNames() const
{
    return {{StableId, "stableId"}, {FileName, "fileName"}, {TransferState, "transferState"},
            {Progress, "progress"}, {SizeLabel, "sizeLabel"}, {DetailLabel, "detailLabel"},
            {IconName, "iconName"}, {DateLabel, "dateLabel"}, {SpeedLabel, "speedLabel"}, {EtaLabel, "etaLabel"},
            {Selected, "isSelected"}};
}

QVariant PreviewDownloads::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const auto &row = m_rows[m_visible[index.row()]];
    switch (role) {
    case StableId: return row.id;
    case FileName: return row.name;
    case TransferState: return row.state;
    case Progress: return row.progress;
    case SizeLabel: return row.size.section('/', -1).trimmed();
    case DetailLabel: return row.detail;
    case IconName: return row.icon;
    // Example rows have no real timestamps or rates; show the same placeholder as the service.
    case DateLabel: case SpeedLabel: case EtaLabel: return QStringLiteral("—");
    case Selected: return m_selection.contains(row.id);
    default: return {};
    }
}

QVariantMap PreviewDownloads::counts() const
{
    QVariantMap result{{"all", m_rows.size()}, {"active", 0}, {"paused", 0},
                       {"completed", 0}, {"failed", 0}, {"unfinished", 0}, {"compressed", 0},
                       {"programs", 0}, {"videos", 0}, {"music", 0}, {"pictures", 0}, {"documents", 0}};
    for (const auto &row : m_rows) {
        const QString key = row.state == "downloading" || row.state == "queued" ? "active" : row.state;
        result[key] = result.value(key).toInt() + 1;
        if (row.state != "completed") result["unfinished"] = result.value("unfinished").toInt() + 1;
        const QString fileCategory = ServiceDownloads::fileCategory(row.name);
        result[fileCategory] = result.value(fileCategory).toInt() + 1;
    }
    return result;
}

void PreviewDownloads::setCategory(const QString &category)
{
    if (!counts().contains(category) || category == m_category) return;
    m_category = category;
    rebuild();
}

void PreviewDownloads::setSearch(const QString &search)
{
    const QString bounded = search.left(256);
    if (m_search == bounded) return;
    m_search = bounded;
    rebuild();
}

void PreviewDownloads::rebuild()
{
    beginResetModel();
    m_visible.clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto &row = m_rows[i];
        const bool active = row.state == "downloading" || row.state == "queued";
        if ((m_category == "all" || m_category == row.state || (m_category == "active" && active)
             || (m_category == "unfinished" && row.state != "completed")
             || m_category == ServiceDownloads::fileCategory(row.name))
            && row.name.contains(m_search.trimmed(), Qt::CaseInsensitive)) m_visible.append(i);
    }
    // Hidden rows are never actionable: drop them from the selection.
    QSet<QString> visible;
    for (int i : std::as_const(m_visible)) visible.insert(m_rows[i].id);
    m_selection.intersect(visible);
    if (!m_selection.contains(m_selectedId)) m_selectedId = m_selection.isEmpty() ? QString() : selectedIds().constFirst();
    if (!visible.contains(m_anchor)) m_anchor = m_selectedId;
    endResetModel();
    emit viewChanged();
    emit selectionChanged();
}

const PreviewDownloads::Row *PreviewDownloads::selected() const
{
    for (const auto &row : m_rows) if (row.id == m_selectedId) return &row;
    return nullptr;
}

QStringList PreviewDownloads::selectedIds() const
{
    QStringList ids; // Visible order, so batch actions and dialogs read top to bottom.
    for (int i : m_visible) if (m_selection.contains(m_rows[i].id)) ids.append(m_rows[i].id);
    return ids;
}

int PreviewDownloads::indexOf(const QString &id) const
{
    for (int i = 0; i < m_visible.size(); ++i) if (m_rows[m_visible[i]].id == id) return i;
    return -1;
}

void PreviewDownloads::setSelection(const QStringList &ids, const QString &current, const QString &anchor)
{
    const QSet<QString> next(ids.cbegin(), ids.cend());
    if (next == m_selection && current == m_selectedId && anchor == m_anchor) return;
    const bool rowsChanged = next != m_selection;
    m_selection = next; m_selectedId = current; m_anchor = anchor;
    if (rowsChanged && !m_visible.isEmpty()) emit dataChanged(index(0), index(m_visible.size() - 1), {Selected});
    emit selectionChanged();
}

void PreviewDownloads::toggleSelected(const QString &id)
{
    if (indexOf(id) < 0) return;
    QStringList ids = selectedIds();
    if (ids.removeAll(id)) setSelection(ids, ids.isEmpty() ? QString() : ids.constLast(), id);
    else setSelection(ids << id, id, id);
}

void PreviewDownloads::selectRange(const QString &id, bool extend)
{
    const int to = indexOf(id);
    if (to < 0) return;
    const int from = indexOf(m_anchor) >= 0 ? indexOf(m_anchor) : to;
    QStringList ids = extend ? selectedIds() : QStringList();
    for (int i = qMin(from, to); i <= qMax(from, to); ++i) if (!ids.contains(idAt(i))) ids.append(idAt(i));
    setSelection(ids, id, from == to ? id : m_anchor);
}

void PreviewDownloads::selectAll()
{
    QStringList ids;
    for (int i : std::as_const(m_visible)) ids.append(m_rows[i].id);
    setSelection(ids, m_selection.contains(m_selectedId) ? m_selectedId : ids.value(0), m_anchor.isEmpty() ? ids.value(0) : m_anchor);
}

QString PreviewDownloads::selectedName() const
{
    const auto *row = selected();
    return row ? row->name : QString();
}

bool PreviewDownloads::canPause() const
{
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [this](const Row &row) {
        return m_selection.contains(row.id) && (row.state == "downloading" || row.state == "queued");
    });
}

bool PreviewDownloads::canResume() const
{
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [this](const Row &row) {
        return m_selection.contains(row.id) && (row.state == "paused" || row.state == "failed");
    });
}

QString PreviewDownloads::idAt(int visibleIndex) const
{
    return data(index(visibleIndex), StableId).toString();
}

void PreviewDownloads::select(const QString &id)
{
    if (id.isEmpty()) { setSelection({}, {}, {}); return; }
    if (indexOf(id) < 0) return;
    setSelection({id}, id, id);
}

bool PreviewDownloads::setSelectedState(const QString &state)
{
    // Every selected row that can make this transition (pause: active; resume: paused/failed).
    const QStringList from = state == "paused" ? QStringList{"downloading", "queued"} : QStringList{"paused", "failed"};
    bool changed = false;
    for (auto &row : m_rows) {
        if (!m_selection.contains(row.id) || !from.contains(row.state)) continue;
        row.state = state;
        row.detail = state == "paused" ? "Paused · preview state only"
                                      : "Queued · preview only; no transfer will start";
        changed = true;
    }
    if (changed) rebuild();
    return changed;
}

bool PreviewDownloads::pauseSelected()
{
    return canPause() && setSelectedState("paused");
}

bool PreviewDownloads::resumeSelected()
{
    return canResume() && setSelectedState("queued");
}

bool PreviewDownloads::removeSelected()
{
    const auto before = m_rows.size();
    m_rows.erase(std::remove_if(m_rows.begin(), m_rows.end(), [this](const Row &row) { return m_selection.contains(row.id); }), m_rows.end());
    if (m_rows.size() == before) return false;
    rebuild();
    return true;
}

bool PreviewDownloads::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    return false;
}

void PreviewDownloads::clearError()
{
    if (m_error.isEmpty()) return;
    m_error.clear();
    emit errorChanged();
}

bool PreviewDownloads::addExample(const QString &source, const QString &fileName)
{
    clearError();
    const QUrl url(source.trimmed(), QUrl::StrictMode);
    if (source.size() > 2048 || !url.isValid() || url.host().isEmpty()
        || (url.scheme() != "https" && url.scheme() != "http") || !url.userInfo().isEmpty())
        return fail(tr("Enter an HTTP or HTTPS example URL without credentials. Nothing will be fetched."));
    const QString name = fileName.trimmed();
    if (name.isEmpty() || name.size() > 180 || name == "." || name == ".."
        || name.contains('/') || name.contains('\\')
        || std::any_of(name.cbegin(), name.cend(), [](QChar c) { return c.isNonCharacter() || c.category() == QChar::Other_Control; }))
        return fail(tr("Enter a file name of 1–180 characters, without path separators or control characters."));
    if (m_rows.size() >= 1000) return fail(tr("The preview is limited to 1,000 example rows."));
    const QString id = QStringLiteral("preview-%1").arg(m_nextId++);
    m_rows.prepend({id, name, "queued", 0.0, "Size unknown",
                    "Queued · preview only; no transfer will start", "text-x-generic"});
    // Make the newly added example visible, even if a filter/search was active.
    m_category = "all";
    m_search.clear();
    m_selectedId = m_anchor = id;
    m_selection = {id};
    rebuild();
    return true;
}
