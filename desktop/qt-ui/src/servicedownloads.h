#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QSet>
#include <QLocalSocket>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

// Snapshot/command adapter only. Never opens profile databases or implements transfers.
class ServiceDownloads : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool liveMode READ liveMode CONSTANT)
    Q_PROPERTY(QVariantMap runtime READ runtime NOTIFY viewChanged)
    Q_PROPERTY(QVariantList queues READ queues NOTIFY viewChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap settingsPolicy READ settingsPolicy NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap integration READ integration NOTIFY viewChanged)
    // A finished queue's power action awaiting the countdown answer, or empty:
    // {serial, type: Shutdown|Sleep|Hibernate, reason: queueFinished|queueEndTime, queueId, queueName}.
    Q_PROPERTY(QVariantMap powerAction READ powerAction NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap checksum READ checksum NOTIFY viewChanged)
    Q_PROPERTY(QStringList capabilities READ capabilities NOTIFY viewChanged)
    Q_PROPERTY(QVariantList browserRequests READ browserRequests NOTIFY browserChanged)
    Q_PROPERTY(QVariantMap browserIntegration READ browserIntegration NOTIFY browserChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY selectionChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY viewChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(int visibleCount READ rowCount NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY viewChanged)
    Q_PROPERTY(QString sortColumn READ sortColumn NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY selectionChanged)
    // Current row of the selection (keyboard focus/anchor end); single-item actions use it.
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(bool canPause READ canPause NOTIFY selectionChanged)
    Q_PROPERTY(bool canResume READ canResume NOTIFY selectionChanged)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY selectionChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    // Stable download ID the current error belongs to (a failed/unknown per-download
    // command), or empty for general errors. Lets a download window show only its own.
    Q_PROPERTY(QString errorTarget READ errorTarget NOTIFY errorChanged)
public:
    explicit ServiceDownloads(const QString &descriptor, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool liveMode() const { return true; }
    QVariantMap runtime() const { return m_runtime; }
    Q_INVOKABLE bool addConfiguredDownload(const QString &url, const QString &name, bool start, const QString &categoryId, const QVariantMap &options);
    Q_INVOKABLE bool editDownload(const QString &id, const QVariantMap &options);
    Q_INVOKABLE bool canMutateDownload(const QString &id, const QString &operation) const;
    Q_INVOKABLE bool resetDownload(const QString &id);
    Q_INVOKABLE bool moveDownload(const QString &id, const QString &name, const QString &folder);
    Q_INVOKABLE bool copyDownloadLinks();
    Q_INVOKABLE QUrl downloadPageUrl(const QString &id) const;
    QVariantList queues() const { return m_queues; }
    QVariantList categories() const { return m_categories; }
    QVariantMap settings() const { return m_settings; }
    QVariantMap settingsPolicy() const { return m_settingsPolicy; }
    QVariantMap integration() const { return m_integration; }
    QVariantMap powerAction() const { return m_powerAction; }
    QVariantMap checksum() const { return m_checksum; }
    Q_INVOKABLE bool checksumCommand(const QString &id, const QString &action, const QString &algorithm, const QString &expected);
    // Confirms (execute) or cancels the pending power action with this serial, once.
    Q_INVOKABLE bool answerPowerAction(const QString &serial, bool execute);
    void setFrontendActive(bool active) { m_frontendActive = active; }
    Q_INVOKABLE bool updateIntegration(const QVariantMap &settings);
    Q_INVOKABLE bool importLinks(const QString &text);
    Q_INVOKABLE bool reviewLinks(const QString &text);
    Q_INVOKABLE bool probeBrowserItems(const QString &requestId, const QVariantList &indices);
    Q_INVOKABLE void importFromClipboard() { emit clipboardImportRequested(); }
    Q_INVOKABLE bool shutdownService();
    Q_INVOKABLE bool taskCommand(const QString &operation);
    QString sizeUnit() const { return m_settings.value("sizeUnit", "BinaryBytes").toString(); }
    Q_INVOKABLE static QString formatSize(qint64 bytes, const QString &unit);
    QStringList capabilities() const { return m_capabilities; }
    // Browser links waiting for a decision. Cookies/headers never leave the service.
    QVariantList browserRequests() const { return m_browserRequests; }
    QVariantMap browserIntegration() const { return m_browserIntegration; }
    // duplicatePolicy: "" (service default, numbered), "numbered" or "overwrite" (same destination only).
    Q_INVOKABLE bool acceptBrowserRequest(const QString &requestId, const QVariantList &items, bool start, const QString &categoryId,
                                          const QString &folder, const QString &duplicatePolicy = {});
    Q_INVOKABLE bool dismissBrowserRequest(const QString &requestId);
    Q_INVOKABLE bool updateBrowserSettings(const QVariantMap &settings);
    // Routine snapshot polling is not "busy": only connection state and an
    // unresolved user command gate the UI, so controls do not flicker every poll.
    bool busy() const { return m_state != "ready" || !m_pending.isEmpty(); }
    Q_INVOKABLE bool updateSettings(const QVariantMap &settings);
    // settings.folder: default destination for new downloads. The effective value is already
    // reported as runtime.destination; a replacement must be an existing folder inside one of
    // the negotiated runtime roots. An empty folder resets to the runtime destination.
    Q_INVOKABLE bool canSetDefaultFolder(const QString &folder) const;
    Q_INVOKABLE bool setDefaultFolder(const QString &folder);
    Q_INVOKABLE bool queueCommand(const QString &operation, const QString &queueId, const QVariantMap &arguments);
    Q_INVOKABLE bool categoryCommand(const QString &operation, const QString &categoryId, const QVariantMap &arguments);
    Q_INVOKABLE QString downloadName(const QString &id) const;
    Q_INVOKABLE QVariantMap downloadDetails(const QString &id) const;
    Q_INVOKABLE bool resumeDownload(const QString &id);
    Q_INVOKABLE bool pauseDownload(const QString &id);
    // Engine-backed state of one download by stable ID, independent of selection/filters.
    // Never contains credentials or headers. {"available": false} once the ID is gone.
    Q_INVOKABLE QVariantMap downloadState(const QString &id) const;
    Q_INVOKABLE bool watchDownloadParts(const QString &id, bool watch);
    Q_INVOKABLE QVariantMap downloadParts(const QString &id) const { return m_parts.value(id).toMap(); }
    // Explicit-runtime only: local URL of a completed regular (non-symlink) file or its folder.
    Q_INVOKABLE QUrl completedFileUrl(const QString &id, bool folder) const;
    // Existing downloads with the same link, or the same folder and file name, from the last
    // snapshot (no IPC). Each entry: id, name, state, sameUrl, sameDestination, fileUrl.
    Q_INVOKABLE QVariantList findDuplicates(const QString &url, const QString &name, const QString &folder) const;
    // A file (or anything) already exists at folder/name, with or without a record.
    Q_INVOKABLE bool destinationExists(const QString &folder, const QString &name) const;
    QString connectionState() const { return m_state; }
    QString category() const { return m_category; }
    QString search() const { return m_search; }
    void setCategory(const QString &value);
    void setSearch(const QString &value);
    QVariantMap counts() const;
    QString sortColumn() const { return m_settings.value("qtSortColumn", "dateAdded").toString(); }
    bool sortDescending() const { return m_settings.value("qtSortDescending", true).toBool(); }
    Q_INVOKABLE bool sortBy(const QString &column);
    QVariantMap statistics() const;
    QString selectedId() const { return m_selected; }
    QStringList selectedIds() const;
    int selectionCount() const { return m_selection.size(); }
    QString selectedName() const;
    bool canPause() const;
    bool canResume() const;
    Q_INVOKABLE QString idAt(int index) const;
    Q_INVOKABLE int indexOf(const QString &id) const;
    // Replaces the selection with one row (empty clears). Also the range anchor.
    Q_INVOKABLE void select(const QString &id);
    // Ctrl+click: adds/removes one row; it becomes current and the new anchor.
    Q_INVOKABLE void toggleSelected(const QString &id);
    // Shift(+Ctrl)+click/arrows: anchor..id in visible order; extend keeps the rest.
    Q_INVOKABLE void selectRange(const QString &id, bool extend = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE bool isSelected(const QString &id) const { return m_selection.contains(id); }
    // Apply to every eligible selected row, one command at a time.
    Q_INVOKABLE bool pauseSelected();
    Q_INVOKABLE bool resumeSelected();
    Q_INVOKABLE bool addToQueue(const QString &queueId, const QStringList &ids);
    Q_INVOKABLE bool assignCategory(const QString &categoryId, const QStringList &ids);
    Q_INVOKABLE bool addExample(const QString &url, const QString &name);
    Q_INVOKABLE bool addDownload(const QString &url, const QString &name, bool start);
    Q_INVOKABLE bool addCategorizedDownload(const QString &url, const QString &name, bool start, const QString &categoryId);
    bool canRemove() const { return canForgetDownloads(selectedIds()); }
    Q_INVOKABLE bool canForgetDownload(const QString &id) const;
    Q_INVOKABLE bool canForgetDownloads(const QStringList &ids) const;
    Q_INVOKABLE bool canDeleteFiles() const { return m_capabilities.contains("download.delete"); }
    Q_INVOKABLE bool forgetDownload(const QString &id, bool deleteFiles = false);
    Q_INVOKABLE bool forgetDownloads(const QStringList &ids, bool deleteFiles = false);
    Q_INVOKABLE bool removeSelected() { return forgetDownloads(selectedIds()); }
    Q_INVOKABLE void clearError();
    QString errorMessage() const { return m_error; }
    QString errorTarget() const { return m_errorTarget; }
    static QString fileCategory(const QString &name);
signals:
    void viewChanged();
    void selectionChanged();
    void errorChanged();
    void browserChanged();
    void clipboardImportRequested();
    void activationRequested(const QString &token, const QString &action, const QString &downloadId);
    // A user-started download started/completed/failed after this frontend connected to the
    // current service epoch. Presentation hint only; each serial is emitted at most once.
    void downloadWindowRequested(const QString &id, const QString &kind);
    // Successful, fully acknowledged acceptance only, after its resulting snapshot.
    // Never emitted for rejection, disconnect/unknown outcome, or an epoch change.
    void browserRequestAccepted(const QString &requestId, const QStringList &ids, bool start);
    void browserRequestDismissed(const QString &requestId);
    // Check Sizes outcome of one review row. state: "pending", "done" or "failed".
    void browserMetadataReady(const QString &requestId, int index, const QString &size, const QString &sizeLabel, const QString &state);
    void serviceStopped();
private:
    void connectService();
    void readFrames();
    void request(const QJsonObject &request);
    bool command(QJsonObject command);
    // Several commands, sent strictly one after another. Stops at the first failure or
    // disconnect; unsent commands are reported and never replayed.
    bool commandBatch(const QList<QJsonObject> &commands);
    void dropBatch(const QString &reason);
    void setSelection(const QStringList &ids, const QString &current, const QString &anchor);
    void disconnected();
    void fail(const QString &message, const QString &target = {});
    void rebuild();
    void updateCounts();
    QJsonObject selected() const;
    bool matchesCategory(const QJsonObject &row, const QString &category) const;
    QString m_descriptor, m_token, m_epoch, m_clientId, m_pending, m_state = "disconnected";
    QString m_category = "all", m_search, m_selected, m_anchor, m_error, m_commandOperation, m_selectOnSnapshot;
    QString m_probeRequest; int m_probeIndex = -1; // In-flight browser.probe, for its failure reply.
    QHash<QString, QString> m_probeSeen; // Review row -> last emitted browser.probeBatch outcome.
    QPair<QString, QString> probeSize(bool requiresAuth, int status, qint64 size) const;
    QSet<QString> m_selection;
    QList<QJsonObject> m_batch;
    QString m_commandTarget, m_errorTarget, m_windowEpoch;
    qint64 m_windowSerial = 0;
    bool m_windowBaseline = true;
    QSet<QString> m_manualPending;
    QHash<QString, int> m_partWatches;
    QVariantMap m_parts;
    QString m_browserCommandId, m_acceptedRequest, m_dismissedRequest;
    QStringList m_acceptedIds;
    bool m_browserCommandStart = false, m_acceptedStart = false;
    QLocalSocket m_socket;
    QTimer m_poll, m_deadline;
    QByteArray m_input;
    QVariantList m_queues, m_categories;
    QVariantMap m_settings, m_counts, m_runtime, m_settingsPolicy, m_browserIntegration, m_integration, m_powerAction, m_checksum;
    QString m_activationKey;
    bool m_frontendActive = false;
    QVariantList m_browserRequests;
    QHash<QString, QSet<QString>> m_categoryMembers;
    QStringList m_capabilities;
    QVector<QJsonObject> m_rows;
    // A user command issued while a snapshot request is in flight. Sent once, right
    // after that reply; dropped (reported as not sent) if the connection ends first.
    QJsonObject m_deferred;
    QVector<int> m_visible;
    quint32 m_expected = 0;
    bool m_waiting = false;
    bool m_uncertain = false;
};
