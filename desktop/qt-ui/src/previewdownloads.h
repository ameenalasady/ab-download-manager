#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVariantMap>
#include <QVector>

// Presentation fixture ONLY. Never performs I/O or represents engine state.
// The future service adapter must replace this model, not call through to it.
class PreviewDownloads : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool liveMode READ liveMode CONSTANT)
    Q_PROPERTY(QString connectionState READ connectionState CONSTANT)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY viewChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(bool canPause READ canPause NOTIFY selectionChanged)
    Q_PROPERTY(bool canResume READ canResume NOTIFY selectionChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

public:
    enum Role { StableId = Qt::UserRole + 1, FileName, TransferState, Progress,
                SizeLabel, DetailLabel, IconName, DateLabel, SpeedLabel, EtaLabel, Selected };
    explicit PreviewDownloads(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool liveMode() const { return false; }
    QString connectionState() const { return QStringLiteral("preview"); }
    int visibleCount() const { return rowCount(); }
    QString category() const { return m_category; }
    QString search() const { return m_search; }
    void setCategory(const QString &category);
    void setSearch(const QString &search);
    QVariantMap counts() const;
    QString selectedId() const { return m_selectedId; }
    QStringList selectedIds() const;
    int selectionCount() const { return m_selection.size(); }
    QString selectedName() const;
    bool canPause() const;
    bool canResume() const;
    QString errorMessage() const { return m_error; }
    Q_INVOKABLE QString idAt(int visibleIndex) const;
    Q_INVOKABLE int indexOf(const QString &id) const;
    // Same selection contract as ServiceDownloads: select/toggle/range/all.
    Q_INVOKABLE void select(const QString &id);
    Q_INVOKABLE void toggleSelected(const QString &id);
    Q_INVOKABLE void selectRange(const QString &id, bool extend = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE bool isSelected(const QString &id) const { return m_selection.contains(id); }
    Q_INVOKABLE bool pauseSelected();
    Q_INVOKABLE bool resumeSelected();
    Q_INVOKABLE bool removeSelected();
    Q_INVOKABLE bool addExample(const QString &source, const QString &fileName);
    Q_INVOKABLE bool addDownload(const QString &source, const QString &fileName, bool start) {
        return !start && addExample(source, fileName);
    }
    Q_INVOKABLE void clearError();

signals:
    void viewChanged();
    void selectionChanged();
    void errorChanged();

private:
    struct Row {
        QString id, name, state;
        double progress;
        QString size, detail, icon;
    };
    const Row *selected() const;
    bool setSelectedState(const QString &state);
    void setSelection(const QStringList &ids, const QString &current, const QString &anchor);
    void rebuild();
    bool fail(const QString &message);
    QVector<Row> m_rows;
    QVector<int> m_visible;
    QString m_category = QStringLiteral("all");
    QString m_search, m_selectedId, m_anchor, m_error;
    QSet<QString> m_selection;
    quint64 m_nextId = 6;
};
