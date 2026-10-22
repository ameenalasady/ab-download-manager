#include "servicedownloads.h"
#include "previewdownloads.h"
#include <QAbstractItemModelTester>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtMath>
#include <QtTest>
#include <memory>
#include <QSignalSpy>

static QByteArray encode(const QJsonObject &object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray result(4, Qt::Uninitialized);
    qToBigEndian<quint32>(bytes.size(), result.data()); result += bytes; return result;
}

class FakeService : public QObject {
public:
    // The native launcher's private TMPDIR can exceed sockaddr_un.sun_path.
    // A short, owned 0700 fixture directory avoids an invalid fake endpoint; it
    // does not relax production descriptor/socket checks or touch profile locks.
    QTemporaryDir directory{QStringLiteral("/tmp/abdm-ui-test-XXXXXX")};
    QLocalServer server;
    QString descriptor;
    QList<QLocalSocket *> clients;
    QJsonArray rows;
    QJsonObject extensionSnapshot, lastCommand, commandResult, partSnapshots;
    bool partsCapability = false, fulfillBrowserAccept = false;
    QJsonArray categorySnapshot;
    bool policyCapability = false;
    bool categoryCapability = false, monitorCapability = false, detailsCapability = false, forgetCapability = false, runtimeCapability = false, httpCapability = false;
    bool browserCapability = false, desktopCapability = false, windowsCapability = false;
    // New negotiated settings/duplicate extensions.
    bool folderCapability = false, duplicateCapability = false, replaceCapability = false;
    // Serial command-batch probes: the full log, a 1-based index that is refused, a
    // mismatched commandId (unexpected reply), and how many snapshots were served.
    QList<QJsonObject> commandLog;
    QList<int> commandSnapshots;
    int failAtCommand = 0, snapshots = 0;
    QString replyCommandId;
    QString epoch = "test-epoch";
    // Simulates a service restart: new epoch published in the descriptor, old clients dropped.
    void restart(const QString &next) {
        epoch = next; writeDescriptor();
        for (auto *client : std::as_const(clients)) client->abort();
    }
    void writeDescriptor() {
        QFile file(descriptor); if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) qFatal("Cannot create test descriptor");
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"epoch", epoch},
            {"token", QString(43, 'x')}, {"socket", directory.filePath("service.sock")}}).toJson());
    }
    QJsonObject lastHello, lastSnapshotRequest;
    int commands = 0;
    bool loseCommandReply = false;
    // Holds snapshot replies so a test can issue a command while one is in flight.
    bool holdSnapshots = false;
    QList<QPair<QLocalSocket *, QByteArray>> held;
    void releaseHeld() { for (const auto &entry : std::as_const(held)) entry.first->write(entry.second); held.clear(); }
    FakeService() {
        const auto socketPath = directory.filePath("service.sock");
        if (!directory.isValid() || !server.listen(socketPath)) qFatal("Cannot create private fake-service socket");
        QFile::setPermissions(socketPath, QFile::ReadOwner | QFile::WriteOwner);
        descriptor = directory.filePath("descriptor.json");
        writeDescriptor();
        rows.append(QJsonObject{{"id", "9007199254740993"}, {"name", "archive.zip"}, {"size", "1000"},
            {"downloaded", "250"}, {"state", "downloading"}, {"dateAdded", "1234"}});
        connect(&server, &QLocalServer::newConnection, this, [this] {
            auto *client = server.nextPendingConnection(); clients.append(client);
            connect(client, &QLocalSocket::readyRead, this, [this, client] {
                QByteArray buffer = client->property("buffer").toByteArray() + client->readAll();
                if (buffer.size() < 4) { client->setProperty("buffer", buffer); return; }
                const auto size = qFromBigEndian<quint32>(buffer.constData());
                if (buffer.size() < size + 4) { client->setProperty("buffer", buffer); return; }
                const auto request = QJsonDocument::fromJson(buffer.mid(4, size)).object();
                client->setProperty("buffer", buffer.mid(size + 4));
                QJsonObject reply{{"epoch", epoch}};
                const auto type = request["type"].toString();
                if (type == "hello") {
                    lastHello = request;
                    reply["type"] = "hello"; reply["version"] = 1;
                    auto caps = QJsonArray{"snapshot", "add", "pause", "resume"};
                    if (!extensionSnapshot.isEmpty()) { caps.append("queue.create"); caps.append("queue.schedule"); caps.append("settings.update"); }
                    if (monitorCapability) caps.append("monitor.stats");
                    if (policyCapability) { caps.append("settings.display"); caps.append("settings.policy"); caps.append("settings.speed"); }
                    if (detailsCapability) caps.append("download.details");
                    if (forgetCapability) caps.append("download.forget");
                    if (runtimeCapability) caps.append("runtime.policy");
                    if (httpCapability) { caps.append("download.edit"); caps.append("download.http-options"); }
                    if (browserCapability) for (const auto &op : {"browser.integration", "browser.intake", "browser.accept", "browser.dismiss", "browser.settings"}) caps.append(op);
                    if (desktopCapability) for (const auto &op : {"settings.integration", "frontend.activate", "links.import", "service.shutdown"}) caps.append(op);
                    if (windowsCapability) caps.append("download.windows");
                    if (partsCapability) caps.append("download.parts");
                    if (folderCapability) caps.append("settings.folder");
                    if (duplicateCapability) caps.append("settings.duplicates");
                    if (replaceCapability) caps.append("download.replace");
                    if (categoryCapability) for (const auto &op : {"category.create", "category.update", "category.delete", "category.move", "category.assign"}) caps.append(op);
                    reply["capabilities"] = caps;
                } else if (type == "snapshot") {
                    lastSnapshotRequest = request; ++snapshots;
                    reply["type"] = "snapshot";
                    auto snapshot = extensionSnapshot;
                    if (categoryCapability) snapshot["categories"] = categorySnapshot;
                    snapshot["state"] = "ready"; snapshot["revision"] = "1"; snapshot["rows"] = rows;
                    if (partsCapability) {
                        QJsonObject projection;
                        for (const auto &id : request["partsFor"].toArray()) projection[id.toString()] = partSnapshots.value(id.toString());
                        snapshot["downloadParts"] = projection;
                    }
                    reply["snapshot"] = snapshot;
                    if (holdSnapshots) { held.append({client, encode(reply)}); return; }
                } else {
                    ++commands; lastCommand = request; commandLog.append(request);
                    commandSnapshots.append(snapshots);
                    if (loseCommandReply) { client->abort(); return; }
                    const bool refused = failAtCommand > 0 && int(commandLog.size()) == failAtCommand;
                    if (request["op"] == "download.forget") {
                        if (!refused && commandResult.isEmpty()) for (int i = rows.size() - 1; i >= 0; --i)
                            if (rows[i].toObject()["id"] == request["id"]) rows.removeAt(i);
                    } else if (!refused && commandResult.isEmpty()
                               && QStringList{"pause", "resume"}.contains(request["op"].toString())) {
                        // Apply to the targeted row only, so a bulk batch is observable per ID.
                        for (int i = 0; i < rows.size(); ++i) {
                            auto row = rows[i].toObject();
                            if (row["id"] != request["id"]) continue;
                            row["state"] = request["op"] == "pause" ? "paused" : "downloading"; rows[i] = row;
                            break;
                        }
                    }
                    reply["type"] = "reply";
                    reply["commandId"] = replyCommandId.isEmpty() ? request["commandId"] : replyCommandId;
                    reply["result"] = refused ? QJsonObject{{"ok", false}, {"error", "Fixture command refusal"}}
                        : (commandResult.isEmpty() ? QJsonObject{{"ok", true}} : commandResult);
                    if (fulfillBrowserAccept && request["op"] == "browser.accept" && reply["result"].toObject()["ok"].toBool()) {
                        QJsonArray ids;
                        for (const auto &value : request["items"].toArray()) {
                            const auto chosen = value.toObject(); const QString id = QString::number(9007199254741001LL + 2 * rows.size()); ids.append(id);
                            rows.append(QJsonObject{{"id", id}, {"name", chosen["name"]}, {"size", "1000"}, {"downloaded", "0"},
                                {"state", request["start"].toBool() ? "downloading" : "paused"}, {"dateAdded", "1234"}});
                        }
                        extensionSnapshot["browserRequests"] = QJsonArray{};
                        reply["result"] = QJsonObject{{"ok", true}, {"id", ids.first()}, {"ids", ids}};
                    }
                }
                // Deliberately fragment both length prefix and JSON body.
                const auto bytes = encode(reply); client->write(bytes.left(2));
                QTimer::singleShot(5, client, [client, bytes] { client->write(bytes.mid(2, 7)); });
                QTimer::singleShot(10, client, [client, bytes] { client->write(bytes.mid(9)); });
            });
        });
    }
};

class ServiceDownloadsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QQuickStyle::setStyle("org.kde.desktop"); QQuickWindow::setDefaultAlphaBuffer(true); }
    void snapshotCommandsAndReconnect() {
        FakeService service;
        ServiceDownloads model(service.descriptor);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.idAt(0), "9007199254740993"); // Stable long ID, not JS-rounded.
        model.select(model.idAt(0));
        QTRY_VERIFY(model.canPause());
        bool paused = false;
        QTRY_VERIFY(paused || (paused = model.pauseSelected()));
        QTRY_VERIFY(model.canResume());
        QCOMPARE(service.commands, 1);
        QCOMPARE(model.selectedId(), "9007199254740993");
        model.setCategory("compressed"); QCOMPARE(model.rowCount(), 1);
        model.setSearch("missing"); QCOMPARE(model.rowCount(), 0); QVERIFY(model.selectedId().isEmpty());
        model.setSearch(""); model.select(model.idAt(0));
        service.loseCommandReply = true;
        bool resumed = false;
        QTRY_VERIFY(resumed || (resumed = model.resumeSelected()));
        QTRY_VERIFY(model.errorMessage().contains("unknown"));
        QTRY_COMPARE(model.connectionState(), "ready");
        QTest::qWait(400);
        QCOMPARE(service.commands, 2); // Lost reply was NOT automatically replayed.
        QVERIFY(model.errorMessage().contains("unknown"));
        QCOMPARE(model.rowCount(), 1);
        QFile::remove(service.descriptor);
        service.clients.last()->abort();
        QTRY_COMPARE(model.connectionState(), "disconnected");
        QVERIFY(!model.canResume()); QCOMPARE(model.rowCount(), 1); // Retain last snapshot, visibly stale.
    }
    void pollingIsQuietAndCommandsBehindSnapshotsAreSentOnce() {
        FakeService service;
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 1);
        model.select(model.idAt(0));
        QTRY_VERIFY(model.canPause());
        // Unchanged snapshots neither toggle busy nor re-notify selection-bound UI.
        QSignalSpy selection(&model, &ServiceDownloads::selectionChanged);
        QTest::qWait(1100);
        QCOMPARE(selection.count(), 0);
        QVERIFY(!model.busy());
        // A command issued while a snapshot is in flight is held, then sent exactly once.
        service.holdSnapshots = true;
        QTRY_VERIFY(!service.held.isEmpty());
        QVERIFY(model.pauseSelected());
        QVERIFY(model.busy()); QVERIFY(!model.canResume());
        QTest::qWait(100); QCOMPARE(service.commands, 0);
        service.holdSnapshots = false; service.releaseHeld();
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["op"].toString(), "pause");
        QTRY_VERIFY(!model.busy()); QTRY_VERIFY(model.canResume());
        QVERIFY(model.errorMessage().isEmpty());
        // A held command whose connection ends first is reported as not sent, and is never sent later.
        service.holdSnapshots = true;
        QTRY_VERIFY(!service.held.isEmpty());
        QVERIFY(model.resumeSelected());
        service.held.clear(); service.clients.last()->abort();
        QTRY_VERIFY(model.errorMessage().contains("not sent"));
        QVERIFY(!model.errorMessage().contains("unknown"));
        service.holdSnapshots = false;
        QTRY_COMPARE(model.connectionState(), "ready");
        QTest::qWait(600);
        QCOMPARE(service.commands, 1);
        QVERIFY(model.canResume());
    }
    void originalSizeUnitsAndRetainedSettingsPolicy() {
        // Original SizeWithUnit "#.##" output for each SupportedSizeUnits value.
        QCOMPARE(ServiceDownloads::formatSize(0, "BinaryBytes"), "0 B");
        QCOMPARE(ServiceDownloads::formatSize(1023, "BinaryBytes"), "1023 B");
        QCOMPARE(ServiceDownloads::formatSize(1536, "BinaryBytes"), "1.5 KiB");
        QCOMPARE(ServiceDownloads::formatSize(8388608, "BinaryBytes"), "8 MiB");
        QCOMPARE(ServiceDownloads::formatSize(128, "BinaryBits"), "1 Kib");
        QCOMPARE(ServiceDownloads::formatSize(1500, "DecimalBytes"), "1.5 KB");
        QCOMPARE(ServiceDownloads::formatSize(125000, "DecimalBits"), "1 Mb");
        QCOMPARE(ServiceDownloads::formatSize(-1, "BinaryBytes"), "Unknown");
        FakeService service; service.policyCapability = true;
        const QJsonObject settings{{"threadCount", 8}, {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3},
            {"minPartSize", "524288"}, {"dynamicPartCreation", true}, {"useSparseFileAllocation", true},
            {"useServerLastModifiedTime", false}, {"sizeUnit", "DecimalBytes"}, {"speedUnit", "BinaryBits"}, {"useCategoryByDefault", false},
            {"useAverageSpeed", false}, {"useSpeedLimit", "Disabled"}, {"speedLimit", "1024"}};
        const QJsonArray retained{QJsonObject{{"key", "apiEnabled"}, {"policy", "platform"}, {"stored", true}, {"value", true}},
            QJsonObject{{"key", "apiAuthKey"}, {"policy", "platform"}, {"stored", true}, {"value", QJsonValue::Null}},
            QJsonObject{{"key", "defaultDownloadFolder"}, {"policy", "runtime-override"}, {"stored", false}, {"value", QJsonValue::Null}}};
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", settings},
            {"settingsPolicy", QJsonObject{{"retained", retained}, {"activityAdapter", false}, {"speedLimitActive", false}}}};
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), PreviewDownloads::SizeLabel).toString(), "1 KB");
        QCOMPARE(model.settingsPolicy()["retained"].toList().size(), 3);
        QVERIFY(model.settingsPolicy()["retained"].toList()[1].toMap()["value"].isNull());
        // Unit changes refresh existing row labels without a row change.
        auto changed = settings; changed["sizeUnit"] = "BinaryBytes"; service.extensionSnapshot["settings"] = changed;
        QTRY_COMPARE(model.data(model.index(0), PreviewDownloads::SizeLabel).toString(), "1000 B");
        // A free-form or secret value is never accepted in the retained view.
        auto leaked = retained; auto entry = leaked[1].toObject(); entry["value"] = "secret"; leaked[1] = entry;
        service.extensionSnapshot["settingsPolicy"] = QJsonObject{{"retained", leaked}, {"activityAdapter", false}, {"speedLimitActive", false}};
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QVERIFY(model.settingsPolicy()["retained"].toList()[1].toMap()["value"].isNull());
        QCOMPARE(model.data(model.index(0), PreviewDownloads::SizeLabel).toString(), "1000 B");
    }
    void settingsFormKeepsStoredBusySpeedModeAndAppliesOnlyItsPage() {
        FakeService service; service.policyCapability = true;
        const QJsonObject settings{{"threadCount", 8}, {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3},
            {"minPartSize", "524288"}, {"dynamicPartCreation", true}, {"useSparseFileAllocation", true},
            {"useServerLastModifiedTime", false}, {"sizeUnit", "BinaryBytes"}, {"speedUnit", "DecimalBits"}, {"useCategoryByDefault", false},
            {"useAverageSpeed", false}, {"useSpeedLimit", "EnabledWhenBessy"}, {"speedLimit", "1024"}};
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", settings},
            {"settingsPolicy", QJsonObject{{"retained", QJsonArray{}}, {"activityAdapter", false}, {"speedLimitActive", false}}}};
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QTRY_COMPARE(model.rowCount(), 1);
        QTRY_VERIFY(model.capabilities().contains("settings.policy"));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openSettings"));
        QObject *dialog = nullptr;
        QTRY_VERIFY((dialog = view.rootObject()->findChild<QObject *>("serviceSettingsDialog")) && dialog->property("opened").toBool());
        auto *mode = view.rootObject()->findChild<QObject *>("settingsSpeedMode");
        auto *notice = view.rootObject()->findChild<QObject *>("settingsBusyModeNotice");
        auto *apply = view.rootObject()->findChild<QObject *>("applyServiceSettings");
        QVERIFY(mode && notice && apply);
        QCOMPARE(mode->property("count").toInt(), 3);
        QCOMPARE(mode->property("currentIndex").toInt(), 2);
        QVERIFY(notice->property("visible").toBool());
        // Applying the Downloads page never rewrites the kept activity-dependent mode.
        QTRY_VERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_COMPARE(service.commands, 1);
        auto patch = service.lastCommand["settings"].toObject();
        QCOMPARE(service.lastCommand["op"].toString(), "settings.update");
        QVERIFY(!patch.contains("useSpeedLimit"));
        QCOMPARE(patch["speedLimit"].toString(), "1024");
        QCOMPARE(patch["useCategoryByDefault"], QJsonValue(false));
        QVERIFY(!patch.contains("sizeUnit"));
        // Choosing another mode is an explicit change.
        mode->setProperty("currentIndex", 0);
        QVERIFY(!notice->property("visible").toBool());
        QTRY_VERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_COMPARE(service.commands, 2);
        QCOMPARE(service.lastCommand["settings"].toObject()["useSpeedLimit"].toString(), "Disabled");
        // The Appearance page applies only the display units.
        dialog->setProperty("page", 0);
        auto *speedUnit = view.rootObject()->findChild<QObject *>("settingsSpeedUnit");
        QVERIFY(speedUnit);
        QCOMPARE(speedUnit->property("currentIndex").toInt(), 3); // DecimalBits
        speedUnit->setProperty("currentIndex", 1);
        QTRY_VERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_COMPARE(service.commands, 3);
        patch = service.lastCommand["settings"].toObject();
        QCOMPARE(patch.keys(), QStringList({"sizeUnit", "speedUnit"}));
        QCOMPARE(patch["sizeUnit"].toString(), "BinaryBytes");
        QCOMPARE(patch["speedUnit"].toString(), "DecimalBytes");
        // Integration page has nothing to apply.
        dialog->setProperty("page", 2);
        QVERIFY(!apply->property("enabled").toBool());
    }
    void rejectsDuplicateIdsAndInsecureDescriptor() {
        FakeService service;
        service.rows.append(service.rows[0]);
        ServiceDownloads model(service.descriptor);
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.rowCount(), 0);
        FakeService insecure;
        QFile::setPermissions(insecure.descriptor, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther);
        ServiceDownloads refused(insecure.descriptor);
        QTRY_VERIFY(refused.errorMessage().contains("not private"));
        QCOMPARE(refused.connectionState(), "disconnected");
    }
    void validatesExtendedSnapshotsAndRetainsLastCoherentState() {
        FakeService service;
        const QJsonObject queue{{"id", "0"}, {"name", "Main"}, {"active", false}, {"maxConcurrent", 2}, {"stopOnEmpty", false},
            {"items", QJsonArray{"9007199254740993"}}, {"schedule", QJsonObject{{"daysOfWeek", QJsonArray{"MONDAY"}},
                {"startTime", "02:30"}, {"endTime", "07:30"}, {"enabledStartTime", false}, {"enabledEndTime", false}}}};
        service.extensionSnapshot = {{"queues", QJsonArray{queue}}, {"settings", QJsonObject{{"threadCount", 8},
            {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3}, {"minPartSize", "524288"},
            {"dynamicPartCreation", true}, {"useSparseFileAllocation", true}, {"useServerLastModifiedTime", false}}}};
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.queues().size(), 1);
        QCOMPARE(model.queues()[0].toMap()["items"].toList()[0].toString(), "9007199254740993");
        QCOMPARE(model.settings()["threadCount"].toInt(), 8);
        QVERIFY(!model.queueCommand("not-a-queue-command", "0", {}));
        // A malformed new queue snapshot cannot erase/partially replace the old one.
        service.extensionSnapshot["queues"] = QJsonArray{queue, queue};
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.queues().size(), 1); QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.settings()["threadCount"].toInt(), 8);
        QVERIFY(!model.updateSettings({{"threadCount", 2}}));
        QCOMPARE(service.commands, 0);
        FakeService invalidSchedule;
        invalidSchedule.extensionSnapshot = service.extensionSnapshot;
        auto broken = queue; auto schedule = queue["schedule"].toObject(); schedule["daysOfWeek"] = QJsonArray{};
        broken["schedule"] = schedule; invalidSchedule.extensionSnapshot["queues"] = QJsonArray{broken};
        ServiceDownloads refused(invalidSchedule.descriptor);
        QTRY_VERIFY(refused.errorMessage().contains("Invalid service response"));
        QVERIFY(refused.queues().isEmpty()); QVERIFY(refused.settings().isEmpty());
    }
    void persistedCategoryFiltersAndMalformedSnapshots() {
        FakeService service; service.categoryCapability = true;
        const QJsonObject category{{"id", "9007199254740997"}, {"name", "Custom"}, {"icon", "icon:pictureFile"},
            {"path", "/fixture/Downloads"}, {"usePath", false}, {"acceptedFileTypes", QJsonArray{"zip"}},
            {"acceptedUrlPatterns", QJsonArray{}}, {"items", QJsonArray{"9007199254740993"}}};
        service.categorySnapshot = QJsonArray{category};
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.categories().size(), 1);
        model.setCategory("category:9007199254740997"); QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.counts()["category:9007199254740997"].toInt(), 1);
        QVERIFY(!model.categoryCommand("queue.add", "0", {}));
        bool assigned = false;
        QTRY_VERIFY(assigned || (assigned = model.categoryCommand("category.assign", "none", {{"id", model.idAt(0)}})));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["categoryId"].toString(), "none");
        QCOMPARE(service.lastCommand["id"].toString(), "9007199254740993");
        // Filtering follows persisted membership, not suffixes. A poll can remove membership.
        auto changed = category; changed["items"] = QJsonArray{}; service.categorySnapshot = QJsonArray{changed};
        QTRY_COMPARE(model.rowCount(), 0);
        changed["items"] = QJsonArray{"9007199254740993", "9007199254740993"}; service.categorySnapshot = QJsonArray{changed};
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.categories().size(), 1);
        QVERIFY(model.categories().first().toMap()["items"].toList().isEmpty()); // Retain coherent previous state.
        FakeService duplicate; duplicate.categoryCapability = true; duplicate.categorySnapshot = QJsonArray{category, category};
        ServiceDownloads refused(duplicate.descriptor);
        QTRY_VERIFY(refused.errorMessage().contains("Invalid service response")); QVERIFY(refused.categories().isEmpty());
    }
    void partialAddSelectsCommittedIdWithoutClearingWarningOrReplaying() {
        FakeService service;
        service.commandResult = {{"ok", false}, {"code", "add-partially-applied"}, {"id", "9007199254740993"},
            {"error", "Download was added but category persistence failed. Inspect the refreshed list before retrying."}};
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 1);
        bool sent = false;
        QTRY_VERIFY(sent || (sent = model.addDownload("http://127.0.0.1:12345/fixture", "archive.zip", false)));
        QTRY_COMPARE(model.selectedId(), "9007199254740993");
        QVERIFY(model.errorMessage().contains("was added"));
        QTest::qWait(400); QCOMPARE(service.commands, 1);
        QVERIFY(model.errorMessage().contains("was added"));
    }
    void validatesRealMonitorMetricsAndUnknownStates() {
        FakeService service; service.monitorCapability = true;
        auto row = service.rows[0].toObject(); row["speed"] = "1048576"; row["remainingTime"] = "125"; service.rows[0] = row;
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), PreviewDownloads::SpeedLabel).toString(), "1 MiB/s");
        QCOMPARE(model.data(model.index(0), PreviewDownloads::EtaLabel).toString(), "2 m 5 s");
        row["speed"] = "-1"; row["remainingTime"] = "-1"; service.rows[0] = row;
        QTRY_COMPARE(model.data(model.index(0), PreviewDownloads::SpeedLabel).toString(), QStringLiteral("—"));
        QCOMPARE(model.data(model.index(0), PreviewDownloads::EtaLabel).toString(), QStringLiteral("—"));
        row["speed"] = 1048576; service.rows[0] = row; // Longs must remain strings.
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.rowCount(), 1);
    }
    void detailsValidateRetainAndRetryCapturedId() {
        FakeService service; service.detailsCapability = true;
        auto row = service.rows[0].toObject(); row["state"] = "failed";
        const QJsonObject reason{{"title", "Something Went Wrong"}, {"description", "<b>not markup</b>"},
            {"suggestion", "Try again"}, {"throwableName", "FixtureException"}, {"throwableMessage", QJsonValue::Null}};
        QJsonObject details{{"url", "http://127.0.0.1:12345/fixture"}, {"folder", "/fixture/Downloads"}, {"error", reason}};
        row["details"] = details; service.rows[0] = row;
        auto second = row; second["id"] = "9007199254740995"; service.rows.append(second);
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(760, 600); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QTRY_COMPARE(model.rowCount(), 2);
        const QString target = model.idAt(0); model.select(target);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openDetails"));
        auto *dialog = view.rootObject()->findChild<QObject *>("downloadDetailsDialog");
        auto *description = view.rootObject()->findChild<QObject *>("downloadErrorDescription");
        auto *retry = view.rootObject()->findChild<QObject *>("retryDownloadDetails");
        QVERIFY(dialog && description && retry);
        QCOMPARE(description->property("text").toString(), "<b>not markup</b>");
        QCOMPARE(description->property("textFormat").toInt(), int(Qt::PlainText));
        model.select(model.idAt(1)); // Open dialog must not silently retarget.
        QCOMPARE(dialog->property("targetId").toString(), target);
        QTRY_VERIFY(retry->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["id"].toString(), target);
        QCOMPARE(service.lastCommand["op"].toString(), "resume");
        // Invalid fields cannot partially replace cached details/rows.
        details["error"] = QJsonObject{{"title", 123}}; row["details"] = details; service.rows[0] = row;
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.downloadDetails(target)["details"].toMap()["error"].toMap()["description"].toString(), "<b>not markup</b>");
        QVERIFY(!model.resumeDownload(target));
        QCOMPARE(service.commands, 1);
        QFile::remove(service.descriptor);
        // Coherent cached details remain readable while disconnected; no replay.
        QVERIFY(!model.downloadDetails(target).isEmpty());
        QTest::qWait(350); QCOMPARE(service.commands, 1);
        FakeService missing; missing.detailsCapability = true;
        ServiceDownloads refused(missing.descriptor);
        QTRY_VERIFY(refused.errorMessage().contains("Invalid service response")); QCOMPARE(refused.rowCount(), 0);
    }
    void forgetKeepsCapturedIdPlainTextAndNeverReplays() {
        FakeService service; service.forgetCapability = true;
        auto row = service.rows[0].toObject(); row["state"] = "paused"; row["name"] = "<b>kept.zip</b>"; service.rows[0] = row;
        auto second = row; second["id"] = "9007199254740995"; service.rows.append(second);
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(760, 600); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QTRY_COMPARE(model.rowCount(), 2); model.select(model.idAt(0));
        QTRY_VERIFY(model.canRemove());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
        auto *dialog = view.rootObject()->findChild<QObject *>("removeExampleDialog");
        auto *description = view.rootObject()->findChild<QObject *>("removeDownloadDescription");
        auto *confirm = view.rootObject()->findChild<QObject *>("confirmRemoveExample");
        QVERIFY(dialog && description && confirm);
        const auto target = model.selectedId(); model.select(model.idAt(1));
        QCOMPARE(dialog->property("targetId").toString(), target);
        QCOMPARE(description->property("textFormat").toInt(), int(Qt::PlainText));
        QVERIFY(description->property("text").toString().contains("<b>kept.zip</b>"));
        QTRY_VERIFY(confirm->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_COMPARE(service.commands, 1); QCOMPARE(service.lastCommand["id"].toString(), target);
        QCOMPARE(service.lastCommand["op"].toString(), "download.forget");
        QCOMPARE(service.lastCommand["keepFiles"], QJsonValue(true));
        QTRY_COMPARE(model.rowCount(), 1); QCOMPARE(model.selectedId(), "9007199254740995");
        // A partial reply stays visible after resnapshot; it is not silently retried.
        service.commandResult = {{"ok", false}, {"code", "remove-partially-applied"}, {"id", model.selectedId()},
            {"error", "Removal may be partially applied. All payload files were kept."}};
        bool sent = false; QTRY_VERIFY(sent || (sent = model.removeSelected()));
        QTRY_VERIFY(model.errorMessage().contains("partially applied"));
        QTest::qWait(400); QCOMPARE(service.commands, 2); QVERIFY(model.errorMessage().contains("partially applied"));
        service.commandResult = {}; service.loseCommandReply = true;
        sent = false; QTRY_VERIFY(sent || (sent = model.removeSelected()));
        QTRY_VERIFY(model.errorMessage().contains("unknown"));
        QTRY_COMPARE(model.connectionState(), "ready"); QTest::qWait(400); QCOMPARE(service.commands, 3);
        QVERIFY(model.errorMessage().contains("unknown"));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
        QFile::remove(service.descriptor); service.clients.last()->abort();
        QTRY_COMPARE(model.connectionState(), "disconnected");
        QVERIFY(!confirm->property("enabled").toBool()); QVERIFY(!model.removeSelected());
        QCOMPARE(model.rowCount(), 1); QCOMPARE(service.commands, 3);
        FakeService legacy; ServiceDownloads unsupported(legacy.descriptor);
        QTRY_COMPARE(unsupported.rowCount(), 1); unsupported.select(unsupported.idAt(0));
        QVERIFY(!unsupported.canRemove()); QVERIFY(!unsupported.removeSelected());
    }
    void explicitRuntimeIsNegotiatedAndInvalidPolicyRetainsCoherentRows() {
        FakeService service; service.runtimeCapability = true; service.httpCapability = true; service.detailsCapability = true;
        auto row = service.rows[0].toObject();
        row["details"] = QJsonObject{{"url", "https://example.invalid/not-fetched"}, {"folder", "/approved/downloads"}, {"error", QJsonValue::Null},
            {"connections", QJsonValue::Null}, {"speedLimit", "0"}, {"hasCredentials", false}};
        service.rows[0] = row;
        service.extensionSnapshot = {{"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/approved/downloads"},
            {"destinationRoots", QJsonArray{"/approved/downloads"}}, {"redirects", true}, {"proxy", "direct"}}},
            {"queues", QJsonArray{}}, {"settings", QJsonObject{{"threadCount", 8}, {"maxConcurrentDownloads", 3},
            {"maxDownloadRetryCount", 3}, {"minPartSize", "524288"}, {"dynamicPartCreation", true},
            {"useSparseFileAllocation", true}, {"useServerLastModifiedTime", false}}}};
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.runtime()["mode"].toString(), "explicit-runtime");
        bool sent = false;
        QTRY_VERIFY(sent || (sent = model.addConfiguredDownload("https://example.invalid/not-fetched", "configured.bin", false, "none",
            {{"folder", "/approved/downloads"}, {"connections", 3}, {"speedLimit", "9007199254740993"}})));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["speedLimit"].toString(), "9007199254740993");
        QCOMPARE(service.lastCommand["folder"].toString(), "/approved/downloads");
        auto broken = service.extensionSnapshot["runtime"].toObject(); broken["mode"] = "implicit-production"; service.extensionSnapshot["runtime"] = broken;
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.runtime()["mode"].toString(), "explicit-runtime"); QCOMPARE(model.rowCount(), 1);
        FakeService legacy; ServiceDownloads isolated(legacy.descriptor);
        QTRY_COMPARE(isolated.rowCount(), 1);
        QVERIFY(!isolated.addDownload("https://example.invalid/file", "x", false));
        QCOMPARE(legacy.commands, 0);
    }
    // Lock the requested client geometry while capturing: a Wayland configure can
    // otherwise supersede resize() and silently produce two equal-size images.
    // Diagnostic context for long real-engine waits: distinguishes a stalled
    // frontend/model from an engine that has not finished.
    static QByteArray modelState(ServiceDownloads &model) {
        QStringList rows;
        for (int i = 0; i < model.rowCount(); ++i)
            rows << model.data(model.index(i), PreviewDownloads::FileName).toString() + "=" + model.data(model.index(i), PreviewDownloads::TransferState).toString();
        return QStringLiteral("state=%1 busy=%2 error=%3 rows=[%4]").arg(model.connectionState()).arg(model.busy())
            .arg(model.errorMessage(), rows.join(", ")).toUtf8();
    }
    static bool reviewFrame(QQuickView &view, const QString &path, const QSize &size) {
        view.setMaximumSize(QSize(16777215, 16777215));
        view.setMinimumSize(size); view.setMaximumSize(size); view.resize(size);
        if (!QTest::qWaitFor([&] { return view.size() == size && view.rootObject()->size() == QSizeF(size); }, 5000)) {
            qWarning() << "reviewFrame geometry timeout" << size << view.size() << view.rootObject()->size(); return false;
        }
        // A configured Wayland size does not imply that a renderable surface is exposed.
        // Only read-only exposure/render waits; never repeat an application command/click.
        if (!QTest::qWaitForWindowExposed(&view, 5000)) {
            qWarning() << "reviewFrame exposure timeout" << view.isVisible() << view.isExposed(); return false;
        }
        QTest::qWait(350);
        // A compositor's first configure can still supersede the request after exposure:
        // re-assert geometry once (window state only, never an application command).
        if (view.size() != size) {
            view.resize(size);
            if (!QTest::qWaitFor([&] { return view.size() == size && view.rootObject()->size() == QSizeF(size); }, 5000)) return false;
            QTest::qWait(350);
        }
        const auto image = view.grabWindow();
        const QSize pixels(qRound(size.width() * view.devicePixelRatio()), qRound(size.height() * view.devicePixelRatio()));
        if (view.size() != size || image.size() != pixels || image.isNull()) {
            qWarning() << "reviewFrame capture mismatch" << "logical" << view.size() << "root" << view.rootObject()->size()
                       << "DPR" << view.devicePixelRatio() << "expected" << pixels << "actual" << image.size()
                       << "imageDPR" << image.devicePixelRatio() << "exposed" << view.isExposed(); return false;
        }
        if (!image.save(path)) { qWarning() << "reviewFrame save failed" << path; return false; }
        // Workflow dialogs now live in native Popup.Window surfaces. A root grab cannot
        // show them: save their OWN clients as companions, without relabelling the root.
        QSet<QQuickWindow *> captured;
        for (auto *object : view.rootObject()->findChildren<QObject *>()) {
            if (!object->property("popupType").isValid() || !object->property("modal").toBool() || !object->property("visible").toBool()) continue;
            auto *content = qvariant_cast<QQuickItem *>(object->property("contentItem"));
            auto *window = content ? content->window() : nullptr;
            if (!window || window == &view || captured.contains(window)) continue;
            captured.insert(window);
            if (!QTest::qWaitForWindowExposed(window, 5000)) return false;
            QTest::qWait(100);
            const auto popupImage = window->grabWindow();
            const auto popupPath = path.chopped(4) + "-" + object->objectName() + ".png";
            if (popupImage.isNull() || !popupImage.save(popupPath)) return false;
        }
        return true;
    }
    // Separate native top-level download windows: same size lock/exposure/DPR checks.
    static bool reviewWindow(QQuickWindow *window, const QString &path, const QSize &requestedSize) {
        // Let state-dependent native minimums (e.g. a disconnect warning) settle before
        // locking. Never demand a capture shorter than the actual content minimum.
        QTest::qWait(200);
        const QSize originalMinimum = window->minimumSize(), originalMaximum = window->maximumSize();
        const QSize size(qMax(requestedSize.width(), window->minimumWidth()), qMax(requestedSize.height(), window->minimumHeight()));
        window->setMinimumSize(size); window->setMaximumSize(size); window->resize(size);
        if (!QTest::qWaitFor([&] { return window->size() == size && window->contentItem()->size() == QSizeF(size); }, 5000)) {
            qWarning() << "reviewWindow geometry timeout" << size << window->size(); return false;
        }
        if (!QTest::qWaitForWindowExposed(window, 5000)) { qWarning() << "reviewWindow exposure timeout"; return false; }
        QTest::qWait(350);
        if (window->size() != size) {
            window->resize(size);
            if (!QTest::qWaitFor([&] { return window->size() == size && window->contentItem()->size() == QSizeF(size); }, 5000)) return false;
            QTest::qWait(350);
        }
        const QSize pixels(qRound(size.width() * window->devicePixelRatio()), qRound(size.height() * window->devicePixelRatio()));
        QImage image;
        // Geometry can precede the rendered buffer, especially when progress becomes the
        // much smaller completion page. Retry only geometry/rendering, never UI actions.
        for (int attempt = 0; attempt < 6; ++attempt) {
            if (window->size() != size) {
                window->setMinimumSize(size); window->setMaximumSize(size); window->resize(size);
            }
            window->requestUpdate(); QTest::qWait(120);
            image = window->grabWindow();
            if (!image.isNull() && image.size() == pixels && window->size() == size) break;
        }
        if (image.isNull() || image.size() != pixels || window->size() != size) { qWarning() << "reviewWindow capture mismatch" << image.size() << pixels; return false; }
        window->setMinimumSize(originalMinimum); window->setMaximumSize(originalMaximum);
        return image.save(path);
    }
    static QQuickWindow *downloadWindow(QQuickView &view, const QString &id) {
        QVariant result;
        QMetaObject::invokeMethod(view.rootObject(), "downloadWindowFor", Q_RETURN_ARG(QVariant, result), Q_ARG(QString, id));
        return qobject_cast<QQuickWindow *>(result.value<QObject *>());
    }
    static int windowCount(QQuickView &view) { return view.rootObject()->property("downloadWindowCount").toInt(); }
    void downloadWindowsFollowStableIdsSettingsAndEpochs() {
        FakeService service; service.windowsCapability = true; service.desktopCapability = true;
        service.monitorCapability = true; service.detailsCapability = true; service.runtimeCapability = true;
        const QString folder = service.directory.filePath("Downloads"); QDir().mkpath(folder);
        const QStringList ids{"9007199254740993", "9007199254740995", "9007199254740997", "9007199254740999", "9007199254741001"};
        service.rows = {};
        for (int i = 0; i < ids.size(); ++i)
            service.rows.append(QJsonObject{{"id", ids[i]}, {"name", i ? QStringLiteral("batch-%1.iso").arg(i) : QStringLiteral("<b>archive</b>.zip")},
                {"size", i == 3 ? "-1" : "1048576000"}, {"downloaded", "262144000"}, {"state", i == 2 ? "queued" : "downloading"},
                {"dateAdded", "1234"}, {"speed", "5242880"}, {"remainingTime", "150"},
                {"details", QJsonObject{{"url", "https://example.invalid/file"}, {"folder", folder}, {"error", QJsonValue::Null}}}});
        auto integration = QJsonObject{{"autoStartOnBoot", false}, {"useSystemTray", true}, {"notificationSound", false},
            {"notifications", true}, {"showDownloadProgressDialog", true}, {"showDownloadCompletionDialog", true}};
        auto hints = [](qint64 head, QList<QPair<QString, QString>> events) {
            QJsonArray list; qint64 serial = head - events.size();
            for (const auto &e : events) list.append(QJsonObject{{"serial", QString::number(++serial)}, {"kind", e.second}, {"id", e.first}});
            return QJsonObject{{"serial", QString::number(head)}, {"events", list}};
        };
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"integration", integration}, {"frontend", QJsonObject{{"activation", QJsonValue::Null}}},
            {"settings", QJsonObject{{"threadCount", 8}, {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3}, {"minPartSize", "524288"},
                {"dynamicPartCreation", true}, {"useSparseFileAllocation", true}, {"useServerLastModifiedTime", false}}},
            {"manualPending", QJsonArray{ids[2]}},
            {"runtime", QJsonObject{{"mode", "isolated"}, {"destination", folder}, {"destinationRoots", QJsonArray{folder}}, {"redirects", false}, {"proxy", "direct"}}},
            // Events that happened before this frontend connected: a baseline, never windows.
            {"downloadWindows", hints(5, {{ids[0], "started"}, {ids[1], "completed"}})}};
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QSignalSpy requested(&model, &ServiceDownloads::downloadWindowRequested);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QTRY_COMPARE(model.rowCount(), 5);
        QTest::qWait(400);
        QCOMPARE(requested.count(), 0); QCOMPARE(windowCount(view), 0);
        QVERIFY(model.downloadState(ids[2])["waiting"].toBool());
        QCOMPARE(model.downloadState(ids[3])["progress"].toDouble(), -1.0); // Unknown size is not estimated.
        QVERIFY(!model.downloadState("404")["available"].toBool());
        QVERIFY(model.completedFileUrl(ids[0], false).isEmpty()); // Not completed, not explicit runtime.
        // One new user start opens one window; unchanged polls and a repeated hint never duplicate it.
        service.extensionSnapshot["downloadWindows"] = hints(6, {{ids[0], "started"}});
        QTRY_COMPARE(windowCount(view), 1);
        auto *first = downloadWindow(view, ids[0]);
        QVERIFY(first && first->transientParent() == nullptr);
        QTRY_VERIFY(first->isVisible());
        QVERIFY(first->property("automatic").toBool());
        QTest::qWait(600); QCOMPARE(requested.count(), 1);
        service.extensionSnapshot["downloadWindows"] = hints(7, {{ids[0], "started"}, {ids[0], "started"}});
        QTRY_COMPARE(requested.count(), 2); QCOMPARE(windowCount(view), 1); QCOMPARE(downloadWindow(view, ids[0]), first);
        auto *name = first->findChild<QObject *>("downloadWindowName");
        auto *bytes = first->findChild<QObject *>("downloadWindowBytes");
        auto *speed = first->findChild<QObject *>("downloadWindowSpeed");
        QVERIFY(name && bytes && speed);
        QCOMPARE(name->property("text").toString(), "<b>archive</b>.zip");
        QCOMPARE(name->property("textFormat").toInt(), int(Qt::PlainText));
        QCOMPARE(bytes->property("text").toString(), "250 MiB of 1000 MiB (25%)");
        QCOMPARE(speed->property("text").toString(), "5 MiB/s");
        QVERIFY(first->title().startsWith("25%"));
        // A browser batch opens at most three automatic windows; the rest stay in the list.
        service.extensionSnapshot["downloadWindows"] = hints(11, {{ids[1], "started"}, {ids[2], "started"}, {ids[3], "started"}, {ids[4], "started"}});
        QTRY_COMPARE(requested.count(), 6);
        QCOMPARE(windowCount(view), 3); QVERIFY(!downloadWindow(view, ids[3]) && !downloadWindow(view, ids[4]));
        // Explicit reopen is never limited.
        model.select(ids[4]);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openDownloadWindow"));
        QCOMPARE(windowCount(view), 4); QVERIFY(!downloadWindow(view, ids[4])->property("automatic").toBool());
        // The window targets its stable ID even after the main selection moves elsewhere.
        model.select(ids[1]);
        auto *pause = first->findChild<QObject *>("downloadWindowPause");
        QVERIFY(pause); QTRY_VERIFY(pause->property("enabled").toBool());
        const int before = service.commands;
        QVERIFY(QMetaObject::invokeMethod(pause, "clicked"));
        QTRY_COMPARE(service.commands, before + 1);
        QCOMPARE(service.lastCommand["op"].toString(), "pause"); QCOMPARE(service.lastCommand["id"].toString(), ids[0]);
        auto *resume = first->findChild<QObject *>("downloadWindowResume");
        QVERIFY(resume); QTRY_VERIFY(resume->property("visible").toBool() && resume->property("enabled").toBool());
        // A rejected command is shown in its own window only, and is not retried.
        service.commandResult = {{"ok", false}, {"code", "command-rejected"}, {"error", "Fixture refusal"}};
        QVERIFY(QMetaObject::invokeMethod(resume, "clicked"));
        QTRY_COMPARE(model.errorTarget(), ids[0]);
        auto *ownError = first->findChild<QObject *>("downloadWindowCommandError");
        auto *otherError = downloadWindow(view, ids[1])->findChild<QObject *>("downloadWindowCommandError");
        QVERIFY(ownError && otherError);
        QTRY_VERIFY(ownError->property("visible").toBool()); QVERIFY(!otherError->property("visible").toBool());
        QTest::qWait(400); QCOMPARE(service.commands, before + 2);
        service.commandResult = {};
        // Closing a window is local: no command, the transfer row is unchanged.
        QVERIFY(QMetaObject::invokeMethod(downloadWindow(view, ids[1]), "close"));
        QTRY_COMPARE(windowCount(view), 3); QTest::qWait(300); QCOMPARE(service.commands, before + 2);
        QCOMPARE(model.downloadState(ids[1])["state"].toString(), "downloading");
        // Completion with the completion window disabled closes an open progress window;
        // failure with it enabled opens one. Settings are read when each hint arrives.
        integration["showDownloadCompletionDialog"] = false; service.extensionSnapshot["integration"] = integration;
        QTRY_VERIFY(!model.integration()["showDownloadCompletionDialog"].toBool());
        service.extensionSnapshot["downloadWindows"] = hints(12, {{ids[2], "completed"}});
        QTRY_VERIFY(!downloadWindow(view, ids[2]));
        integration["showDownloadCompletionDialog"] = true; integration["showDownloadProgressDialog"] = false;
        service.extensionSnapshot["integration"] = integration;
        QTRY_VERIFY(!model.integration()["showDownloadProgressDialog"].toBool());
        const int open = windowCount(view);
        service.extensionSnapshot["downloadWindows"] = hints(14, {{ids[3], "started"}, {ids[1], "failed"}});
        QTRY_VERIFY(downloadWindow(view, ids[1])); QVERIFY(!downloadWindow(view, ids[3]));
        QCOMPARE(windowCount(view), open + 1);
        // Disconnect keeps windows and last state but disables their controls; reconnecting to
        // the same epoch at the same serial opens nothing and replays no command.
        const int commands = service.commands, windows = windowCount(view), hinted = requested.count();
        service.clients.last()->abort();
        QTRY_COMPARE(model.connectionState(), "disconnected");
        auto *stale = first->findChild<QObject *>("downloadWindowStale");
        QVERIFY(stale); QTRY_VERIFY(stale->property("visible").toBool()); QVERIFY(!resume->property("enabled").toBool());
        QTRY_COMPARE(model.connectionState(), "ready"); QTest::qWait(500);
        QCOMPARE(windowCount(view), windows); QCOMPARE(requested.count(), hinted); QCOMPARE(service.commands, commands);
        // A same-epoch serial regression is malformed and rejected as a whole.
        service.extensionSnapshot["downloadWindows"] = hints(3, {{ids[4], "started"}});
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(requested.count(), hinted);
        // A service restart (new epoch, serials restart) is a fresh baseline: nothing reopens.
        model.clearError();
        service.extensionSnapshot["downloadWindows"] = hints(2, {{ids[0], "started"}, {ids[1], "failed"}});
        service.restart("restarted-epoch");
        QTRY_COMPARE(model.connectionState(), "ready"); QTest::qWait(500);
        QCOMPARE(requested.count(), hinted); QVERIFY(model.errorMessage().isEmpty());
        integration["showDownloadProgressDialog"] = true; service.extensionSnapshot["integration"] = integration;
        QTRY_VERIFY(model.integration()["showDownloadProgressDialog"].toBool());
        QVERIFY(QMetaObject::invokeMethod(downloadWindow(view, ids[4]), "close"));
        QTRY_VERIFY(!downloadWindow(view, ids[4]));
        service.extensionSnapshot["downloadWindows"] = hints(3, {{ids[4], "started"}});
        QTRY_VERIFY(downloadWindow(view, ids[4])); QCOMPARE(requested.count(), hinted + 1);
        // Removed downloads keep their window, explicitly unavailable, files untouched.
        service.rows.removeAt(4);
        auto *removedStatus = downloadWindow(view, ids[4])->findChild<QObject *>("downloadWindowStatus");
        QVERIFY(removedStatus); QTRY_VERIFY(removedStatus->property("text").toString().contains("No longer"));
        const QString capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            auto failedRow = service.rows[1].toObject(); failedRow["state"] = "failed";
            failedRow["details"] = QJsonObject{{"url", "https://example.invalid/batch-1.iso"}, {"folder", folder},
                {"error", QJsonObject{{"title", "Something Went Wrong"}, {"description", "Server responded with 404 Not Found."},
                    {"suggestion", "Check the link, then retry."}, {"throwableName", "FixtureException"}, {"throwableMessage", QJsonValue::Null}}}};
            service.rows[1] = failedRow;
            QTRY_COMPARE(model.downloadState(ids[1])["state"].toString(), "failed");
            auto activeRow = service.rows[0].toObject(); activeRow["state"] = "downloading"; service.rows[0] = activeRow;
            QTRY_COMPARE(model.downloadState(ids[0])["state"].toString(), "downloading");
            QVERIFY(reviewWindow(first, capture + "/window-downloading.png", QSize(520, 340)));
            QVERIFY(reviewWindow(first, capture + "/window-downloading-narrow.png", QSize(360, 340)));
            QVERIFY(reviewWindow(downloadWindow(view, ids[1]), capture + "/window-failed.png", QSize(520, 420)));
            QVERIFY(reviewWindow(downloadWindow(view, ids[4]), capture + "/window-removed.png", QSize(520, 300)));
            activeRow["state"] = "paused"; service.rows[0] = activeRow;
            QTRY_COMPARE(model.downloadState(ids[0])["state"].toString(), "paused");
            QVERIFY(reviewWindow(first, capture + "/window-paused.png", QSize(520, 300)));
        }
        // Completed: Open File/Folder only for a real regular file in the explicit runtime.
        {
            auto runtime = service.extensionSnapshot["runtime"].toObject(); runtime["mode"] = "explicit-runtime"; service.extensionSnapshot["runtime"] = runtime;
            auto done = service.rows[2].toObject(); done["state"] = "completed"; done["downloaded"] = done["size"]; done["name"] = "batch-2.iso"; service.rows[2] = done;
            auto linked = service.rows[3].toObject(); linked["state"] = "completed"; linked["size"] = "4"; linked["downloaded"] = "4"; service.rows[3] = linked;
            QFile payload(QDir(folder).filePath("batch-2.iso")); QVERIFY(payload.open(QIODevice::WriteOnly)); payload.write("data"); payload.close();
            QVERIFY(QFile::link(QDir(folder).filePath("batch-2.iso"), QDir(folder).filePath("batch-3.iso")));
            QTRY_COMPARE(model.downloadState(ids[2])["state"].toString(), "completed");
            QTRY_COMPARE(model.runtime()["mode"].toString(), "explicit-runtime");
            QCOMPARE(model.completedFileUrl(ids[2], false), QUrl::fromLocalFile(QDir(folder).filePath("batch-2.iso")));
            QCOMPARE(model.completedFileUrl(ids[2], true), QUrl::fromLocalFile(QDir(folder).absolutePath()));
            QVERIFY(model.completedFileUrl(ids[3], false).isEmpty()); // Symlink refused.
            QVERIFY(model.completedFileUrl(ids[0], false).isEmpty()); // Not completed.
            QVariant shown;
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "showDownloadWindow", Q_RETURN_ARG(QVariant, shown), Q_ARG(QString, ids[2]), Q_ARG(bool, false)));
            auto *done2 = downloadWindow(view, ids[2]);
            QVERIFY(done2);
            auto *openFile = done2->findChild<QObject *>("downloadWindowOpenFile");
            auto *progress = done2->findChild<QObject *>("downloadWindowProgress");
            QVERIFY(openFile && progress);
            QTRY_VERIFY(openFile->property("visible").toBool());
            QVERIFY(!progress->property("visible").toBool()); // Distinct completion page, not the progress pane.
            QVERIFY(done2->findChild<QObject *>("downloadCompletionHeading"));
            if (!capture.isEmpty()) QVERIFY(reviewWindow(done2, capture + "/window-completed.png", QSize(520, 280)));
        }
        if (!capture.isEmpty()) {
            service.clients.last()->abort(); QFile::remove(service.descriptor);
            QTRY_COMPARE(model.connectionState(), "disconnected");
            QVERIFY(reviewWindow(first, capture + "/window-disconnected.png", QSize(520, 380)));
        }
    }
    static QJsonObject basicSettings() {
        return {{"threadCount", 8}, {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3}, {"minPartSize", "524288"},
            {"dynamicPartCreation", true}, {"useSparseFileAllocation", true}, {"useServerLastModifiedTime", false}};
    }
    // Five rows in service order: two pausable, one resumable, one completed, one failed.
    static QStringList seedRows(FakeService &service) {
        const QStringList ids{"9007199254740993", "9007199254740995", "9007199254740997", "9007199254740999", "9007199254741001"};
        const QStringList names{"alpha.zip", "beta.txt", "gamma.mp4", "delta.pdf", "epsilon.png"};
        const QStringList states{"downloading", "paused", "queued", "completed", "failed"};
        service.rows = {};
        for (int i = 0; i < ids.size(); ++i)
            service.rows.append(QJsonObject{{"id", ids[i]}, {"name", names[i]}, {"size", "1000"},
                {"downloaded", "250"}, {"state", states[i]}, {"dateAdded", "1234"}});
        return ids;
    }
    static QString stateOf(ServiceDownloads &model, const QString &id) {
        return model.downloadState(id)["state"].toString();
    }
    void multiSelectionRangesPruneHiddenRowsAndStayStable() {
        FakeService service;
        const QStringList ids = seedRows(service);
        ServiceDownloads model(service.descriptor);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QTRY_COMPARE(model.rowCount(), 5);
        QCOMPARE(model.idAt(0), ids[0]);
        // Ctrl+click toggles single rows; the selection is always reported in visible order.
        model.toggleSelected(ids[2]);
        QCOMPARE(model.selectedIds(), QStringList{ids[2]});
        model.toggleSelected(ids[0]);
        QCOMPARE(model.selectedIds(), (QStringList{ids[0], ids[2]}));
        QCOMPARE(model.selectedId(), ids[0]);
        QCOMPARE(model.selectedName(), "alpha.zip");
        QCOMPARE(model.selectionCount(), 2);
        model.toggleSelected(ids[0]);
        QCOMPARE(model.selectedIds(), QStringList{ids[2]});
        QCOMPARE(model.data(model.index(0), PreviewDownloads::Selected).toBool(), false);
        // Shift range from the anchor replaces; shift grows and shrinks from the same anchor.
        model.select(ids[1]);
        model.selectRange(ids[4]);
        QCOMPARE(model.selectedIds(), (QStringList{ids[1], ids[2], ids[3], ids[4]}));
        QCOMPARE(model.selectedId(), ids[4]);
        model.selectRange(ids[3]);
        QCOMPARE(model.selectedIds(), (QStringList{ids[1], ids[2], ids[3]}));
        // Shift+Ctrl keeps what is already selected.
        model.select(ids[3]);
        model.selectRange(ids[1], true);
        QCOMPARE(model.selectedIds(), (QStringList{ids[1], ids[2], ids[3]}));
        model.selectAll();
        QCOMPARE(model.selectedIds(), ids);
        // Filtering prunes hidden rows permanently; clearing the filter does not resurrect them.
        model.setSearch("alpha");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.selectedIds(), QStringList{ids[0]});
        model.setSearch("");
        QCOMPARE(model.selectedIds(), QStringList{ids[0]});
        model.selectAll();
        QCOMPARE(model.selectedIds(), ids);
        model.setCategory("videos");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.selectedIds(), QStringList{ids[2]});
        model.setCategory("all");
        QCOMPARE(model.selectionCount(), 1);
        QVERIFY(model.isSelected(ids[2]));
        // Unchanged polls never renotify selection-bound UI.
        QSignalSpy selection(&model, &ServiceDownloads::selectionChanged);
        QTest::qWait(600);
        QCOMPARE(selection.count(), 0);
        QCOMPARE(model.selectedIds(), QStringList{ids[2]});
        // A snapshot that only changes state keeps the selection by stable ID.
        auto row = service.rows[2].toObject(); row["state"] = "downloading"; service.rows[2] = row;
        QTRY_COMPARE(stateOf(model, ids[2]), "downloading");
        QCOMPARE(model.selectedIds(), QStringList{ids[2]});
        // Removing the selected row drops it from the selection instead of aliasing a neighbour.
        service.rows.removeAt(2);
        QTRY_COMPARE(model.rowCount(), 4);
        QVERIFY(model.selectedIds().isEmpty());
        QVERIFY(model.selectedId().isEmpty());
        QCOMPARE(model.indexOf(ids[1]), 1);
        QVERIFY(model.errorMessage().isEmpty());
    }
    void bulkPauseAndResumeSendOneCommandAtATime() {
        FakeService service;
        const QStringList ids = seedRows(service);
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 5);
        model.selectAll();
        QCOMPARE(model.selectedIds(), ids);
        QVERIFY(model.canPause());
        QVERIFY(model.canResume());
        QVERIFY(model.pauseSelected());
        QTRY_COMPARE(service.commandLog.size(), 2); // Only the two active rows.
        QCOMPARE(service.commandLog[0]["op"].toString(), "pause");
        QCOMPARE(service.commandLog[0]["id"].toString(), ids[0]);
        QCOMPARE(service.commandLog[1]["id"].toString(), ids[2]);
        // Each step waits for the previous reply and snapshot: never two in flight.
        QVERIFY(service.commandSnapshots[1] > service.commandSnapshots[0]);
        QTRY_VERIFY(!model.busy());
        QTRY_COMPARE(stateOf(model, ids[0]), "paused");
        QCOMPARE(stateOf(model, ids[2]), "paused");
        QCOMPARE(stateOf(model, ids[1]), "paused");
        QCOMPARE(stateOf(model, ids[3]), "completed");
        QCOMPARE(stateOf(model, ids[4]), "failed");
        QVERIFY(model.errorMessage().isEmpty());
        QCOMPARE(model.selectedIds(), ids); // The batch never loses the user's selection.
        // Resume targets every resumable row (paused, failed), still one command at a time.
        QVERIFY(model.resumeSelected());
        QTRY_COMPARE(service.commandLog.size(), 6);
        QCOMPARE(service.commandLog[2]["op"].toString(), "resume");
        QCOMPARE(service.commandLog[2]["id"].toString(), ids[0]);
        QCOMPARE(service.commandLog[3]["id"].toString(), ids[1]);
        QCOMPARE(service.commandLog[4]["id"].toString(), ids[2]);
        QCOMPARE(service.commandLog[5]["id"].toString(), ids[4]);
        QVERIFY(service.commandSnapshots[3] > service.commandSnapshots[2]);
        QVERIFY(service.commandSnapshots[5] > service.commandSnapshots[4]);
        QTRY_VERIFY(!model.busy());
        QTRY_COMPARE(stateOf(model, ids[0]), "downloading");
        QCOMPARE(stateOf(model, ids[3]), "completed");
        // A selection with nothing eligible sends no command at all.
        model.select(ids[3]);
        QVERIFY(!model.canPause());
        QVERIFY(!model.canResume());
        QVERIFY(!model.pauseSelected());
        QVERIFY(!model.resumeSelected());
        QTest::qWait(400);
        QCOMPARE(service.commandLog.size(), 6);
        QVERIFY(model.errorMessage().isEmpty());
    }
    void bulkBatchStopsAtFirstRefusedCommandWithoutReplay() {
        FakeService service;
        const QStringList ids = seedRows(service);
        service.failAtCommand = 2;
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 5);
        // Three active rows, so the batch has a third step that must never be sent.
        for (const int index : {1, 2}) {
            auto row = service.rows[index].toObject(); row["state"] = "downloading"; service.rows[index] = row;
        }
        QTRY_COMPARE(stateOf(model, ids[2]), "downloading");
        model.selectAll();
        QVERIFY(model.pauseSelected());
        QTRY_VERIFY(model.errorMessage().contains("Fixture command refusal"));
        QVERIFY(model.errorMessage().contains("not sent"));
        QCOMPARE(model.errorTarget(), ids[1]); // The refused command's own stable ID.
        QCOMPARE(service.commandLog.size(), 2);
        QCOMPARE(service.commandLog[1]["id"].toString(), ids[1]);
        QTest::qWait(600); // Polling must never resurrect the dropped third step.
        QCOMPARE(service.commandLog.size(), 2);
        QCOMPARE(model.connectionState(), "ready");
        QCOMPARE(stateOf(model, ids[0]), "paused");
        QCOMPARE(stateOf(model, ids[1]), "downloading");
        QCOMPARE(stateOf(model, ids[2]), "downloading");
        QVERIFY(model.canPause());
        // A refused row can still be acted on later: that is a fresh user action, not a replay.
        model.clearError();
        service.failAtCommand = 0;
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.pauseSelected());
        QTRY_COMPARE(service.commandLog.size(), 4);
        QTRY_COMPARE(stateOf(model, ids[1]), "paused");
        QTRY_COMPARE(stateOf(model, ids[2]), "paused");
    }
    void bulkBatchDisconnectReportsUnknownAndNeverResends() {
        FakeService service;
        const QStringList ids = seedRows(service);
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 5);
        model.selectAll();
        service.loseCommandReply = true; // The first pause is applied, its reply never arrives.
        QVERIFY(model.pauseSelected());
        QTRY_COMPARE(model.connectionState(), "disconnected");
        QTRY_VERIFY(model.errorMessage().contains("unknown")); // Transmitted: outcome unknown...
        QVERIFY(model.errorMessage().contains("not sent"));   // ...and the queued step was dropped.
        QCOMPARE(service.commandLog.size(), 1);
        QTRY_COMPARE(model.connectionState(), "ready");
        QTest::qWait(700);
        QCOMPARE(service.commandLog.size(), 1); // The queued step is never replayed automatically.
        QVERIFY(model.errorMessage().contains("unknown"));
        // An explicit retry is allowed and is the only thing that clears the warning.
        service.loseCommandReply = false;
        QVERIFY(model.pauseSelected());
        QTRY_COMPARE(service.commandLog.size(), 3);
        QTRY_VERIFY(model.errorMessage().isEmpty());
        QTRY_COMPARE(stateOf(model, ids[0]), "paused");
        QCOMPARE(stateOf(model, ids[2]), "paused");
    }
    void unexpectedCommandReplyDropsTheBatchAndNeverClaimsSuccess() {
        FakeService service;
        const QStringList ids = seedRows(service);
        service.replyCommandId = "not-the-command-we-sent";
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 5);
        model.selectAll();
        QVERIFY(model.pauseSelected());
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QVERIFY(model.errorMessage().contains("unknown"));
        QVERIFY(model.errorMessage().contains("not sent"));
        QCOMPARE(service.commandLog.size(), 1);
        QTRY_COMPARE(model.connectionState(), "ready");
        QTest::qWait(700);
        QCOMPARE(service.commandLog.size(), 1);
        QCOMPARE(stateOf(model, ids[2]), "queued"); // The dropped step was never sent.
        QVERIFY(!model.errorMessage().isEmpty());
    }
    void bulkRemovalForgetsEveryEligibleRowInOneBatch() {
        FakeService service; service.forgetCapability = true;
        const QStringList ids = seedRows(service);
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 5);
        model.select(ids[1]);
        model.toggleSelected(ids[3]);
        model.toggleSelected(ids[4]);
        QVERIFY(model.canRemove());
        QVERIFY(model.removeSelected());
        QTRY_COMPARE(service.commandLog.size(), 3);
        QCOMPARE(service.commandLog[0]["op"].toString(), "download.forget");
        QCOMPARE(service.commandLog[0]["id"].toString(), ids[1]);
        QCOMPARE(service.commandLog[1]["id"].toString(), ids[3]);
        QCOMPARE(service.commandLog[2]["id"].toString(), ids[4]);
        for (const auto &command : std::as_const(service.commandLog))
            QCOMPARE(command["keepFiles"], QJsonValue(true));
        QTRY_COMPARE(model.rowCount(), 2);
        QCOMPARE(model.idAt(0), ids[0]);
        QCOMPARE(model.idAt(1), ids[2]);
        // One still-active row makes the whole removal unavailable: no half batch.
        model.select(ids[0]);
        model.toggleSelected(ids[2]);
        QCOMPARE(model.selectionCount(), 2);
        QVERIFY(!model.canRemove());
        QVERIFY(!model.removeSelected());
        QCOMPARE(service.commandLog.size(), 3);
        // A refusal inside a removal stops the rest and is reported against its own ID.
        auto row = service.rows[0].toObject(); row["state"] = "paused"; service.rows[0] = row;
        QTRY_COMPARE(stateOf(model, ids[0]), "paused");
        QVERIFY(model.canRemove());
        service.failAtCommand = 4;
        QVERIFY(model.removeSelected());
        QTRY_VERIFY(model.errorMessage().contains("Fixture command refusal"));
        QVERIFY(model.errorMessage().contains("not sent"));
        QCOMPARE(model.errorTarget(), ids[0]);
        QCOMPARE(service.commandLog.size(), 4);
        QTest::qWait(500);
        QCOMPARE(service.commandLog.size(), 4);
        QCOMPARE(model.rowCount(), 2);
    }
    void duplicateDialogsRequireAnExplicitChoiceBeforeAdding() {
        FakeService service; service.detailsCapability = true; service.runtimeCapability = true;
        service.duplicateCapability = true; service.replaceCapability = true; service.browserCapability = true;
        const QString folder = service.directory.filePath("Downloads"); QVERIFY(QDir().mkpath(folder));
        const QString id = "9007199254740993", link = "https://example.invalid/archive.zip";
        service.rows = QJsonArray{QJsonObject{{"id", id}, {"name", "archive.zip"}, {"size", "1000"},
            {"downloaded", "1000"}, {"state", "completed"}, {"dateAdded", "1234"},
            {"details", QJsonObject{{"url", link}, {"folder", folder}, {"error", QJsonValue::Null}}}}};
        auto settings = basicSettings(); settings["qtDuplicateAction"] = "ask";
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", settings},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", folder}, {"destinationRoots", QJsonArray{folder}}, {"redirects", true}, {"proxy", "direct"}}},
            {"browserRequests", QJsonArray{}}, {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready); QTRY_COMPARE(model.connectionState(), "ready");
        QTRY_COMPARE(model.rowCount(), 1);
        QTRY_COMPARE(model.settings()["qtDuplicateAction"].toString(), "ask");
        const auto object = [&](const char *name) { return view.rootObject()->findChild<QObject *>(name); };
        const auto openAdd = [&] {
            QMetaObject::invokeMethod(view.rootObject(), "openAddDialog");
            object("exampleUrlInput")->setProperty("text", link);
            object("exampleNameInput")->setProperty("text", "archive.zip");
        };
        openAdd(); QVERIFY(QMetaObject::invokeMethod(object("confirmAddExample"), "clicked"));
        auto *duplicate = object("duplicateDownloadDialog"); QVERIFY(duplicate);
        QTRY_VERIFY(duplicate->property("visible").toBool()); QCOMPARE(service.commands, 0);
        QVERIFY(object("duplicateOverwrite")->property("enabled").toBool());
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            auto *content = qvariant_cast<QQuickItem *>(duplicate->property("contentItem")); QVERIFY(content && content->window());
            QVERIFY(reviewWindow(content->window(), capture + "/duplicate-main.png", QSize(640, 360)));
        }
        QVERIFY(QMetaObject::invokeMethod(object("duplicateAddNew"), "clicked"));
        QTRY_COMPARE(service.commands, 1); QCOMPARE(service.lastCommand["duplicatePolicy"].toString(), "numbered");
        QTRY_VERIFY(!model.busy()); QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
        // Existing means show only: no automatic resume, execution or new Add.
        settings["qtDuplicateAction"] = "existing"; service.extensionSnapshot["settings"] = settings;
        QTRY_COMPARE(model.settings()["qtDuplicateAction"].toString(), "existing");
        openAdd(); QVERIFY(QMetaObject::invokeMethod(object("confirmAddExample"), "clicked"));
        QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
        QCOMPARE(service.commands, 1); QVERIFY(downloadWindow(view, id));
        // Browser intake uses the same safe numbered policy only after a review choice.
        settings["qtDuplicateAction"] = "ask"; service.extensionSnapshot["settings"] = settings;
        service.extensionSnapshot["browserRequests"] = QJsonArray{QJsonObject{{"id", "duplicate-browser"}, {"outcomeUnknown", false},
            {"items", QJsonArray{QJsonObject{{"url", link}, {"name", "archive.zip"}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}}}}}};
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view.rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        auto *accept = review->findChild<QObject *>("acceptBrowserRequest"); QVERIFY(accept);
        QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
        auto *browserDuplicate = review->findChild<QObject *>("browserDuplicateDialog"); QVERIFY(browserDuplicate);
        QTRY_VERIFY(browserDuplicate->property("visible").toBool()); QCOMPARE(service.commands, 1);
        service.commandResult = QJsonObject{{"ok", true}, {"id", id}, {"ids", QJsonArray{id}}};
        QVERIFY(QMetaObject::invokeMethod(review->findChild<QObject *>("browserDuplicateAddNew"), "clicked"));
        QTRY_COMPARE(service.commands, 2);
        QCOMPARE(service.lastCommand["op"].toString(), "browser.accept");
        QCOMPARE(service.lastCommand["duplicatePolicy"].toString(), "numbered");
        QCOMPARE(service.lastCommand["requestId"].toString(), "duplicate-browser");
    }
    void duplicateLookupDefaultFolderAndDuplicatePolicy() {
        FakeService service; service.detailsCapability = true; service.runtimeCapability = true;
        service.folderCapability = true; service.duplicateCapability = true;
        const QString folder = service.directory.filePath("Downloads");
        const QString nested = QDir(folder).filePath("nested");
        const QString outside = service.directory.filePath("outside");
        QVERIFY(QDir().mkpath(nested));
        QVERIFY(QDir().mkpath(outside));
        service.rows = {};
        service.rows.append(QJsonObject{{"id", "9007199254740993"}, {"name", "archive.zip"}, {"size", "1000"},
            {"downloaded", "1000"}, {"state", "completed"}, {"dateAdded", "1234"},
            {"details", QJsonObject{{"url", "https://example.invalid/a.zip"}, {"folder", folder}, {"error", QJsonValue::Null}}}});
        service.rows.append(QJsonObject{{"id", "9007199254740995"}, {"name", "other.bin"}, {"size", "1000"},
            {"downloaded", "0"}, {"state", "paused"}, {"dateAdded", "1234"},
            {"details", QJsonObject{{"url", "https://example.invalid/other"}, {"folder", nested}, {"error", QJsonValue::Null}}}});
        auto settings = basicSettings(); settings.insert("qtDuplicateAction", "ask");
        auto runtime = QJsonObject{{"mode", "explicit-runtime"}, {"destination", folder},
            {"destinationRoots", QJsonArray{folder}}, {"redirects", true}, {"proxy", "direct"}};
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", settings}, {"runtime", runtime}};
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 2);
        QCOMPARE(model.settings().value("qtDuplicateAction").toString(), "ask");
        QCOMPARE(model.downloadState("9007199254740993")["iconName"].toString(), "package-x-generic");
        // The link is normalised exactly as Add stores it, so typed text still matches.
        const auto byUrl = model.findDuplicates("https://EXAMPLE.invalid/a.zip", "", "");
        QCOMPARE(byUrl.size(), 1);
        QCOMPARE(byUrl[0].toMap()["id"].toString(), "9007199254740993");
        QVERIFY(byUrl[0].toMap()["sameUrl"].toBool());
        QVERIFY(!byUrl[0].toMap()["sameDestination"].toBool());
        QCOMPARE(byUrl[0].toMap()["state"].toString(), "completed");
        // The same name in the same folder matches for another link, and offers the file.
        QFile payload(QDir(folder).filePath("archive.zip"));
        QVERIFY(payload.open(QIODevice::WriteOnly)); payload.close();
        const auto byDestination = model.findDuplicates("https://example.invalid/unrelated", "archive.zip", folder);
        QCOMPARE(byDestination.size(), 1);
        QVERIFY(byDestination[0].toMap()["sameDestination"].toBool());
        QVERIFY(!byDestination[0].toMap()["sameUrl"].toBool());
        QCOMPARE(byDestination[0].toMap()["fileUrl"].toUrl(), QUrl::fromLocalFile(QDir(folder).filePath("archive.zip")));
        // An empty or relative folder can never alias an absolute stored destination.
        QVERIFY(model.findDuplicates("", "archive.zip", "").isEmpty());
        QVERIFY(model.findDuplicates("", "archive.zip", "Downloads").isEmpty());
        QVERIFY(model.findDuplicates("", "other.bin", folder).isEmpty());
        // Existing destinations follow the Add name rules and include dangling links.
        QVERIFY(model.destinationExists(folder, "archive.zip"));
        QVERIFY(!model.destinationExists(folder, "missing.zip"));
        QVERIFY(!model.destinationExists(folder, "../archive.zip"));
        QVERIFY(!model.destinationExists(folder, ".."));
        QVERIFY(!model.destinationExists(folder, ""));
        QVERIFY(!model.destinationExists(QStringLiteral("Downloads"), "archive.zip"));
        QVERIFY(QFile::link(QDir(folder).filePath("does-not-exist"), QDir(folder).filePath("dangling.zip")));
        QVERIFY(model.destinationExists(folder, "dangling.zip"));
        // The default folder must already exist inside a negotiated runtime root.
        QVERIFY(model.canSetDefaultFolder(folder));
        QVERIFY(model.canSetDefaultFolder(nested));
        QVERIFY(!model.canSetDefaultFolder(QStringLiteral("/")));
        QVERIFY(!model.canSetDefaultFolder(QStringLiteral("nested")));
        QVERIFY(!model.canSetDefaultFolder(QDir(folder).filePath("missing")));
        QVERIFY(!model.canSetDefaultFolder(outside));
        QVERIFY(QFile::link(nested, QDir(folder).filePath("linked")));
        QVERIFY(!model.canSetDefaultFolder(QDir(folder).filePath("linked"))); // Symlinks are refused.
        QVERIFY(model.setDefaultFolder(nested));
        QTRY_COMPARE(service.commandLog.size(), 1);
        QCOMPARE(service.commandLog[0]["op"].toString(), "settings.update");
        auto patch = service.commandLog[0]["settings"].toObject();
        QCOMPARE(patch.keys(), QStringList{"defaultDownloadFolder"});
        QCOMPARE(patch["defaultDownloadFolder"].toString(), nested);
        QTRY_VERIFY(!model.busy());
        // Outside the roots is refused locally: no round-trip and no silent widening.
        QVERIFY(!model.setDefaultFolder(outside));
        QVERIFY(model.errorMessage().contains("destination roots"));
        QCOMPARE(service.commandLog.size(), 1);
        // An empty folder resets to the runtime destination, as the original stores it.
        QVERIFY(model.setDefaultFolder(QString()));
        QTRY_COMPARE(service.commandLog.size(), 2);
        QCOMPARE(service.commandLog[1]["settings"].toObject()["defaultDownloadFolder"].toString(), QString());
        QTRY_VERIFY(!model.busy());
        // Overwrite is only offered when the service negotiates download.replace.
        QVERIFY(!model.addConfiguredDownload("https://example.invalid/fresh.zip", "fresh.zip", false, "none",
            {{"duplicatePolicy", "overwrite"}}));
        QVERIFY(model.errorMessage().contains("cannot replace"));
        QVERIFY(!model.addConfiguredDownload("https://example.invalid/fresh.zip", "fresh.zip", false, "none",
            {{"duplicatePolicy", "yolo"}}));
        QVERIFY(model.errorMessage().contains("Unsupported duplicate policy"));
        QCOMPARE(service.commandLog.size(), 2);
        QVERIFY(model.addConfiguredDownload("https://example.invalid/fresh.zip", "fresh.zip", false, "none",
            {{"duplicatePolicy", "numbered"}}));
        QTRY_COMPARE(service.commandLog.size(), 3);
        QCOMPARE(service.commandLog[2]["duplicatePolicy"].toString(), "numbered");
        QTRY_VERIFY(!model.busy());
        service.replaceCapability = true;
        service.restart("replace-epoch");
        QTRY_VERIFY(model.capabilities().contains("download.replace"));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.addConfiguredDownload("https://example.invalid/fresh.zip", "fresh.zip", false, "none",
            {{"duplicatePolicy", "overwrite"}}));
        QTRY_COMPARE(service.commandLog.size(), 4);
        QCOMPARE(service.commandLog[3]["duplicatePolicy"].toString(), "overwrite");
        QTRY_VERIFY(!model.busy());
        // A stored duplicate action outside the original values is rejected as a whole snapshot.
        auto broken = service.extensionSnapshot["settings"].toObject();
        broken["qtDuplicateAction"] = "overwrite";
        service.extensionSnapshot["settings"] = broken;
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.settings().value("qtDuplicateAction").toString(), "ask");
        broken["qtDuplicateAction"] = "existing";
        service.extensionSnapshot["settings"] = broken;
        QTRY_COMPARE(model.settings().value("qtDuplicateAction").toString(), "existing");
        // Without settings.folder nothing is promised or sent.
        service.folderCapability = false;
        service.restart("no-folder-epoch");
        QTRY_VERIFY(!model.capabilities().contains("settings.folder"));
        QTRY_VERIFY(!model.busy());
        QVERIFY(!model.canSetDefaultFolder(nested));
        QVERIFY(!model.setDefaultFolder(nested));
        QCOMPARE(service.commandLog.size(), 4);
        // A default folder outside the runtime roots never replaces the last coherent one.
        auto stray = runtime;
        stray["destination"] = outside;
        service.extensionSnapshot["runtime"] = stray;
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.runtime().value("destination").toString(), folder);
        QVERIFY(!model.canSetDefaultFolder(outside));
    }
    void progressShowsEightRowsAndResizesToDistinctCompletion() {
        FakeService service; service.partsCapability = true;
        const QString id = service.rows.first().toObject()["id"].toString();
        QJsonArray parts;
        for (int i = 0; i < 8; ++i) parts.append(QJsonObject{{"id", QString::number(i * 125)}, {"offset", QString::number(i * 125)},
            {"length", "125"}, {"downloaded", i < 2 ? "125" : "0"}, {"state", i < 2 ? "completed" : "connecting"}});
        service.partSnapshots[id] = QJsonObject{{"available", true}, {"totalCount", 8}, {"truncated", false}, {"resumeSupport", true}, {"items", parts}};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready); QTRY_COMPARE(model.connectionState(), "ready");
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "showDownloadWindow", Q_ARG(QString, id), Q_ARG(bool, false)));
        auto *window = downloadWindow(view, id); QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(model.downloadParts(id)["items"].toList().size(), 8);
        QTRY_COMPARE(window->width(), 560);
        QTest::qWait(1800); // Includes late compositor restores, not just the first configure.
        auto *list = window->findChild<QQuickItem *>("downloadWindowPartList"); QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 8);
        QTRY_VERIFY(list->height() >= list->property("contentHeight").toReal() - 1);
        QTRY_VERIFY(list->mapRectToScene(QRectF(QPointF(), list->size())).bottom() <= window->height());
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/progress-eight-default.png"));
        const int hold = qBound(0, qEnvironmentVariableIntValue("QT_PROGRESS_CHROME_HOLD_MS"), 10000);
        // A synthetic test has no user activation token; hide its owned main window so
        // it cannot occlude the separate progress surface in compositor-only captures.
        if (hold) { view.hide(); QTest::qWait(hold); }
        qInfo() << "natural progress size (eight rows)" << window->size();
        QVERIFY(list->mapRectToScene(QRectF(QPointF(), list->size())).bottom() <= window->height());
        const int progressHeight = window->height();
        auto done = service.rows.first().toObject(); done["state"] = "completed"; done["downloaded"] = "1000"; service.rows[0] = done;
        QObject *heading = nullptr;
        QTRY_VERIFY((heading = window->findChild<QObject *>("downloadCompletionHeading")) && heading->property("visible").toBool());
        QTRY_VERIFY(window->height() < progressHeight);
        QVERIFY(!list->isVisible());
        QVERIFY(!window->findChild<QObject *>("downloadWindowProgress")->property("visible").toBool());
        QTest::qWait(350);
        qInfo() << "natural completion size" << window->size();
        if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/completion-default.png"));
        if (hold) QTest::qWait(hold);
    }
    // Sizing regression guard. Each toplevel is created hidden, measured from its own
    // content and only then mapped, so the geometry it is first exposed/rendered with is
    // the settled one. Nothing here resizes, minimises or otherwise forces a geometry: a
    // window that opens at a shared default and is re-fitted by a timer fails below.
    static QJsonObject completedFirstRow(FakeService &service) {
        auto done = service.rows.first().toObject();
        done["state"] = "completed"; done["downloaded"] = "1000";
        service.rows[0] = done;
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}};
        return done;
    }
    static QQuickView *liveView(ServiceDownloads &model) {
        auto *view = new QQuickView;
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view->setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view->resize(1000, 700); view->show();
        return view;
    }
    // Read-only observation: the first rendered frame's geometry, and the geometry the
    // surface is first exposed with.
    static void watchFirstFrame(QQuickWindow *window) {
        QObject::connect(window, &QQuickWindow::frameSwapped, window, [window] {
            if (!window->property("firstFrameSize").isValid()) window->setProperty("firstFrameSize", window->size());
        });
    }
    static QSize firstExposedSize(QQuickWindow *window) {
        QSize exposed;
        const bool ready = QTest::qWaitFor([&] {
            if (!window->isExposed()) return false;
            exposed = window->size();
            return true;
        }, 5000);
        return ready ? exposed : QSize();
    }
    void verifyExposedSizeIsSettled(QQuickWindow *window, const QSize &exposed, const QString &label) {
        QVERIFY2(exposed.isValid(), qPrintable(label + QStringLiteral(": never exposed")));
        const QVariant firstFrame = window->property("firstFrameSize");
        QVERIFY2(!firstFrame.isValid() || firstFrame.toSize() == exposed,
            qPrintable(QStringLiteral("%1: first frame %2x%3, exposed %4x%5").arg(label)
                .arg(firstFrame.toSize().width()).arg(firstFrame.toSize().height())
                .arg(exposed.width()).arg(exposed.height())));
        QVERIFY2(window->size() == exposed, qPrintable(QStringLiteral("%1: settled %2x%3, exposed %4x%5").arg(label)
            .arg(window->width()).arg(window->height()).arg(exposed.width()).arg(exposed.height())));
    }
    void progressToplevelIsExposedAtItsSettledSize() {
        FakeService service; service.partsCapability = true;
        const QString id = service.rows.first().toObject()["id"].toString();
        QJsonArray parts;
        for (int i = 0; i < 8; ++i) parts.append(QJsonObject{{"id", QString::number(i * 125)}, {"offset", QString::number(i * 125)},
            {"length", "125"}, {"downloaded", "0"}, {"state", "connecting"}});
        service.partSnapshots[id] = QJsonObject{{"available", true}, {"totalCount", 8}, {"truncated", false},
            {"resumeSupport", true}, {"items", parts}};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QCOMPARE(view->status(), QQuickView::Ready); QTRY_COMPARE(model.connectionState(), "ready");
        // Deliberately leave the parts cache cold. Initial presentation must wait for
        // the window's own first watch response, not rely on a pre-hydrated test fixture.
        QVERIFY(model.downloadParts(id).isEmpty());
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "showDownloadWindow", Q_ARG(QString, id), Q_ARG(bool, false)));
        auto *window = downloadWindow(*view, id); QVERIFY(window);
        watchFirstFrame(window);
        const QSize exposed = firstExposedSize(window);
        auto *list = window->findChild<QQuickItem *>("downloadWindowPartList"); QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 8);
        QTest::qWait(1800); // Includes a late compositor restore and the 150 ms guard pass.
        verifyExposedSizeIsSettled(window, exposed, "progress");
        auto *content = window->findChild<QObject *>("downloadContent"); QVERIFY(content);
        const int natural = qCeil(content->property("contentHeight").toReal());
        QVERIFY(natural > 0);
        // The adopted height is the content's own size, clamped to the usable screen.
        const int available = qRound(window->property("availableHeight").toReal());
        QCOMPARE(window->height(), qMax(window->minimumHeight(), qMin(natural, available)));
        QVERIFY2(window->height() != 760, "the shared Tokens.downloadWindowHeight default must not be the exposed size");
        // The eight part rows are on the first surface, not clipped by it.
        const QRectF rows = list->mapRectToScene(QRectF(QPointF(), list->size()));
        QVERIFY2(rows.bottom() <= exposed.height(), qPrintable(QStringLiteral("part rows bottom %1 exposed %2")
            .arg(rows.bottom()).arg(exposed.height())));
        qInfo() << "exposed progress size" << exposed << "natural" << natural << "available" << available;
    }
    void completionToplevelIsGenuinelyContentSized() {
        FakeService service; completedFirstRow(service);
        const QString id = service.rows.first().toObject()["id"].toString();
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QCOMPARE(view->status(), QQuickView::Ready); QTRY_COMPARE(model.connectionState(), "ready");
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "showDownloadWindow", Q_ARG(QString, id), Q_ARG(bool, false)));
        auto *window = downloadWindow(*view, id); QVERIFY(window);
        watchFirstFrame(window);
        const QSize exposed = firstExposedSize(window);
        QObject *heading = nullptr;
        QTRY_VERIFY((heading = window->findChild<QObject *>("downloadCompletionHeading")) && heading->property("visible").toBool());
        QTest::qWait(1800);
        verifyExposedSizeIsSettled(window, exposed, "completion");
        auto *content = window->findChild<QObject *>("downloadContent"); QVERIFY(content);
        const int natural = qCeil(content->property("contentHeight").toReal());
        QVERIFY(natural > 0);
        QCOMPARE(window->height(), natural);
        QCOMPARE(window->minimumHeight(), natural);
        // Tokens.windowMinimumHeight is 260 for every window role; this page is smaller.
        QVERIFY2(window->height() < 260, qPrintable(QStringLiteral("completion height %1").arg(window->height())));
        // The summary genuinely fits: its last action is inside the exposed surface.
        auto *close = window->findChild<QQuickItem *>("downloadCompletionClose"); QVERIFY(close && close->isVisible());
        const QRectF bounds = close->mapRectToScene(QRectF(QPointF(), close->size()));
        QVERIFY(bounds.top() >= 0);
        QVERIFY2(bounds.bottom() <= exposed.height(), qPrintable(QStringLiteral("close bottom %1 exposed %2")
            .arg(bounds.bottom()).arg(exposed.height())));
        qInfo() << "exposed completion size" << exposed << "natural" << natural;
    }
    // The browser review is the same native surface that later shows progress. Both its
    // first exposure and the transition to progress must be fitted, not re-fitted later.
    void browserReviewIsFittedBeforeShowAndOnProgressTransition() {
        FakeService service; service.browserCapability = true; service.windowsCapability = true;
        service.fulfillBrowserAccept = true; service.rows = {};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}, {"browserRequests", QJsonArray{}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}},
            {"downloadWindows", QJsonObject{{"serial", "0"}, {"events", QJsonArray{}}}}};
        QJsonArray items;
        for (int i = 0; i < 3; ++i) items.append(QJsonObject{{"url", QStringLiteral("https://example.invalid/%1").arg(i)},
            {"name", QStringLiteral("review-%1.bin").arg(i)}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}});
        service.extensionSnapshot["browserRequests"] = QJsonArray{QJsonObject{{"id", "fit-review"}, {"items", items},
            {"outcomeUnknown", false}, {"source", "browser"}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QCOMPARE(view->status(), QQuickView::Ready); QTRY_COMPARE(model.connectionState(), "ready");
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view->rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        watchFirstFrame(review);
        const QSize exposed = firstExposedSize(review);
        QTest::qWait(1200);
        verifyExposedSizeIsSettled(review, exposed, "review");
        // Natural: the adopted height is the measured page, not the shared review default.
        double measured = 0;
        QVERIFY(QMetaObject::invokeMethod(review, "measuredHeight", Q_RETURN_ARG(double, measured)));
        QVERIFY(measured > 0);
        QCOMPARE(review->height(), review->minimumHeight());
        QVERIFY2(review->height() != 580, "the shared Tokens.reviewWindowHeight default must not be exposed");
        qInfo() << "exposed review size" << exposed << "measured" << qCeil(measured);
        auto *accept = review->findChild<QObject *>("acceptBrowserRequest"); QVERIFY(accept);
        QTRY_VERIFY(accept->property("enabled").toBool());
        const int chromeHold = qBound(0, qEnvironmentVariableIntValue("QT_BROWSER_CHROME_HOLD_MS"), 10000);
        if (chromeHold) {
            view->hide();
            if (qEnvironmentVariableIsSet("QT_CAPTURE_KEYBOARD_FOCUS"))
                qobject_cast<QQuickItem *>(accept)->forceActiveFocus(Qt::TabFocusReason);
            QTest::qWait(chromeHold);
        }
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) QVERIFY(review->grabWindow().save(capture + "/browser-natural.png"));
        QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
        // The review turns into a progress window in place. Its size must be the progress
        // page's own at that moment, not the review height kept until a guard timer fires.
        QQuickItem *content = nullptr;
        QVERIFY(QTest::qWaitFor([&] {
            if (!content) content = review->findChild<QQuickItem *>("downloadContent");
            return content && content->property("contentHeight").toReal() > 0;
        }, 5000));
        const QSize transitioned = review->size();
        const int progressNatural = qCeil(content->property("contentHeight").toReal());
        QVERIFY(progressNatural > 0);
        QVERIFY2(review->width() == 560, "the progress page uses the download width");
        QVERIFY2(transitioned.height() != exposed.height(), qPrintable(QStringLiteral("transition kept the review height %1")
            .arg(transitioned.height())));
        QTest::qWait(1200);
        QVERIFY2(review->size() == transitioned, qPrintable(QStringLiteral("transitioned %1x%2 settled %3x%4")
            .arg(transitioned.width()).arg(transitioned.height()).arg(review->width()).arg(review->height())));
        QCOMPARE(review->height(), review->minimumHeight());
        qInfo() << "transitioned progress size" << transitioned << "natural" << progressNatural;
        // Completion on the adopted browser surface has the same content-sized floor
        // as a standalone completion, without an extra generic minimum or bottom gap.
        const auto nativeId = review->winId();
        auto done = service.rows.first().toObject();
        done["state"] = "completed"; done["downloaded"] = done["size"];
        service.rows[0] = done;
        QTRY_VERIFY(review->property("completedView").toBool());
        QTest::qWait(1800);
        QCOMPARE(review->winId(), nativeId);
        QCOMPARE(review->height(), qCeil(content->property("contentHeight").toReal()));
        QVERIFY(review->height() < 260);
        qInfo() << "adopted completion size" << review->size();
    }
    void boundedPartsAreWatchedValidatedAndCredentialFree() {
        FakeService service; service.partsCapability = true;
        const QString id = service.rows.first().toObject()["id"].toString();
        const QJsonObject part{{"id", "0"}, {"offset", "0"}, {"length", "1000"}, {"downloaded", "250"}, {"state", "downloading"}};
        service.partSnapshots[id] = QJsonObject{{"available", true}, {"totalCount", 1}, {"truncated", false}, {"resumeSupport", true}, {"items", QJsonArray{part}}};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}};
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 1);
        QVERIFY(service.lastSnapshotRequest["partsFor"].toArray().isEmpty());
        QVERIFY(model.watchDownloadParts(id, true));
        QTRY_VERIFY(model.downloadParts(id).value("available").toBool());
        const auto coherent = model.downloadParts(id);
        QCOMPARE(coherent["items"].toList().first().toMap()["progress"].toDouble(), 0.25);
        QCOMPARE(coherent["items"].toList().first().toMap()["id"].toString(), "0");
        // A future/buggy service must not smuggle extra text into the QML projection.
        auto extra = part; extra["headers"] = "must-not-leak";
        auto data = service.partSnapshots[id].toObject(); data["items"] = QJsonArray{extra}; service.partSnapshots[id] = data;
        QTest::qWait(400);
        QVERIFY(!model.downloadParts(id)["items"].toList().first().toMap().contains("headers"));
        QVERIFY(model.watchDownloadParts(id, true)); // Refcounted duplicate interest.
        QVERIFY(model.watchDownloadParts(id, false));
        QCOMPARE(service.commands, 0);
        auto invalid = part; invalid["downloaded"] = "1001"; data["items"] = QJsonArray{invalid}; service.partSnapshots[id] = data;
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.downloadParts(id), coherent); // Reject the whole malformed snapshot.
        QVERIFY(model.watchDownloadParts(id, false));
        for (int i = 0; i < 16; ++i) QVERIFY(model.watchDownloadParts(QString::number(i), true));
        QVERIFY(!model.watchDownloadParts("99", true));
        QVERIFY(!model.watchDownloadParts("not-an-id", true));
        QCOMPARE(service.commands, 0);
    }
    void nativeReviewTransitionBatchTrayLaterAndUnknown() {
        FakeService service; service.browserCapability = true; service.windowsCapability = true;
        service.fulfillBrowserAccept = true; service.rows = {};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}, {"browserRequests", QJsonArray{}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}},
            {"downloadWindows", QJsonObject{{"serial", "0"}, {"events", QJsonArray{}}}}};
        auto request = [](const QString &id, int count, bool unknown = false) {
            QJsonArray items;
            for (int i = 0; i < count; ++i) items.append(QJsonObject{{"url", QStringLiteral("https://example.invalid/%1").arg(i)},
                {"name", QStringLiteral("batch-%1.bin").arg(i)}, {"hasHeaders", true}, {"downloadPage", QJsonValue::Null}});
            return QJsonObject{{"id", id}, {"items", items}, {"outcomeUnknown", unknown}};
        };
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QSignalSpy accepted(&model, &ServiceDownloads::browserRequestAccepted);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready); QTRY_COMPARE(model.connectionState(), "ready");
        // Main hidden in tray: a NEW request opens an independent toplevel, not the list.
        view.hide(); service.extensionSnapshot["browserRequests"] = QJsonArray{request("batch", 4)};
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view.rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        QVERIFY(!view.isVisible()); QVERIFY(review->transientParent() == nullptr);
        QVERIFY(!(review->flags() & Qt::FramelessWindowHint));
        const WId surface = review->winId();
        auto *accept = review->findChild<QObject *>("acceptBrowserRequest"); QVERIFY(accept);
        QTRY_VERIFY(accept->property("enabled").toBool()); QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
        QTRY_COMPARE(accepted.count(), 1); QTRY_COMPARE(windowCount(view), 3);
        const auto ids = accepted.first()[1].toStringList(); QCOMPARE(ids.size(), 4);
        QCOMPARE(downloadWindow(view, ids.first()), review); QCOMPARE(review->winId(), surface);
        QVERIFY(review->isVisible()); QVERIFY(!view.isVisible()); QVERIFY(!downloadWindow(view, ids.last()));
        QVERIFY(review->property("footer").value<QObject *>());
        QTRY_COMPARE(review->property("footer").value<QObject *>()->property("height").toInt(), 0);
        QCOMPARE(service.commands, 1);
        // Later, window-manager Close and unchanged/reconnected snapshots don't accept/replay.
        service.extensionSnapshot["browserRequests"] = QJsonArray{request("later", 1)};
        QPointer<QQuickWindow> next;
        QTRY_VERIFY((next = view.rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && next->isVisible());
        auto *later = next->findChild<QObject *>("laterBrowserRequest"); QVERIFY(later);
        QVERIFY(QMetaObject::invokeMethod(later, "clicked")); QTRY_VERIFY(!next);
        QTest::qWait(400); QCOMPARE(service.commands, 1);
        QVERIFY(!view.rootObject()->findChild<QObject *>("browserRequestDialog"));
        service.restart("review-new-epoch"); QTRY_COMPARE(model.connectionState(), "ready");
        QTest::qWait(400); QCOMPARE(accepted.count(), 1); QCOMPARE(windowCount(view), 3);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "reviewBrowserRequests"));
        QTRY_VERIFY((next = view.rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && next->isVisible());
        // Unknown reservations cannot be replayed; Discard remains available.
        service.extensionSnapshot["browserRequests"] = QJsonArray{request("later", 1, true)};
        QTRY_VERIFY(next->property("outcomeUnknown").toBool());
        QVERIFY(!next->findChild<QObject *>("acceptBrowserRequest")->property("enabled").toBool());
        QVERIFY(!model.acceptBrowserRequest("later", {QVariantMap{{"index", 0}, {"url", "https://example.invalid/0"}, {"name", "batch-0.bin"}}}, true, "auto", ""));
        QCOMPARE(service.commands, 1);
        auto *discard = next->findChild<QObject *>("dismissBrowserRequest"); QTRY_VERIFY(discard->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(discard, "clicked")); QTRY_COMPARE(service.commands, 2); QTRY_VERIFY(!next);
        service.extensionSnapshot["browserRequests"] = QJsonArray{}; QTRY_VERIFY(model.browserRequests().isEmpty());
        // Add Paused acknowledges into the list without progress windows (even at cap).
        service.extensionSnapshot["browserRequests"] = QJsonArray{request("paused", 1)};
        QTRY_VERIFY((next = view.rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && next->isVisible());
        auto *paused = next->findChild<QObject *>("pauseBrowserRequest"); QTRY_VERIFY(paused->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(paused, "clicked")); QTRY_COMPARE(accepted.count(), 2); QTRY_VERIFY(!next);
        QCOMPARE(windowCount(view), 3); QCOMPARE(service.commands, 3);
        QCOMPARE(model.downloadState(accepted.last()[1].toStringList().first())["state"].toString(), "paused");
        // Lost reply: keep the review and its durable unknown reservation, never transition.
        service.extensionSnapshot["browserRequests"] = QJsonArray{request("lost", 1)};
        QTRY_VERIFY((next = view.rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && next->isVisible());
        service.loseCommandReply = true;
        accept = next->findChild<QObject *>("acceptBrowserRequest"); QTRY_VERIFY(accept->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(accept, "clicked")); QTRY_COMPARE(service.commands, 4);
        service.extensionSnapshot["browserRequests"] = QJsonArray{request("lost", 1, true)};
        QTRY_VERIFY(model.errorMessage().contains("unknown")); QTRY_COMPARE(model.connectionState(), "ready");
        QTRY_VERIFY(next->property("outcomeUnknown").toBool()); QTest::qWait(400);
        QCOMPARE(service.commands, 4); QCOMPARE(accepted.count(), 2); QCOMPARE(windowCount(view), 3);
        QVERIFY(next->isVisible()); QVERIFY(!view.isVisible());
    }
    void captureOnlyHttpOptions() {
        // Reconstruct only the UI state preceding the retained failure, against a fake
        // read-only endpoint. No Add, Start, Retry, settings write or real profile access.
        FakeService service; service.httpCapability = true; service.rows = {};
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 650); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QTRY_COMPARE(model.connectionState(), "ready");
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
        auto *options = view.rootObject()->findChild<QObject *>("openAddHttpOptions");
        QVERIFY(options); QTRY_VERIFY(options->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(options, "clicked"));
        const QString capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QVERIFY(reviewFrame(view, capture + "/capture-only-http.png", QSize(1000, 650)));
            QVERIFY(reviewFrame(view, capture + "/capture-only-http-small.png", QSize(760, 600)));
        }
        QCOMPARE(service.commands, 0);
    }
    void browserRequestsAreValidatedAndAcceptedWithoutSecrets() {
        FakeService service; service.browserCapability = true;
        const QJsonObject integration{{"state", "listening"}, {"error", QJsonValue::Null}, {"enabled", true}, {"port", 15151}, {"auth", false}};
        const QJsonObject pending{{"id", "req-1"}, {"receivedAt", "1"}, {"items", QJsonArray{
            QJsonObject{{"url", "https://example.invalid/a.zip"}, {"name", "a.zip"}, {"hasHeaders", true}, {"downloadPage", QJsonValue::Null}},
            QJsonObject{{"url", "https://example.invalid/b.zip"}, {"name", "b.zip"}, {"hasHeaders", false}, {"downloadPage", "https://example.invalid/"}}}}};
        service.extensionSnapshot = {}; service.rows = {};
        QJsonObject snapshot{{"browserIntegration", integration}, {"browserRequests", QJsonArray{pending}}};
        service.extensionSnapshot = snapshot;
        // extensionSnapshot non-empty adds queue/settings capabilities; satisfy their validation.
        service.extensionSnapshot["queues"] = QJsonArray{};
        service.extensionSnapshot["settings"] = QJsonObject{{"threadCount", 8}, {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3},
            {"minPartSize", "2097152"}, {"dynamicPartCreation", true}, {"useSparseFileAllocation", true}, {"useServerLastModifiedTime", false}};
        ServiceDownloads model(service.descriptor);
        QSignalSpy browser(&model, &ServiceDownloads::browserChanged);
        QTRY_COMPARE(model.browserRequests().size(), 1);
        QCOMPARE(service.lastHello["role"].toString(), "frontend");
        QCOMPARE(model.browserIntegration()["state"].toString(), "listening");
        QCOMPARE(model.browserRequests()[0].toMap()["items"].toList().size(), 2);
        const int notified = browser.count();
        QTest::qWait(400); QCOMPARE(browser.count(), notified); // Unchanged polls stay quiet.
        // Client-side validation before sending; nothing is sent for an empty selection.
        QVERIFY(!model.acceptBrowserRequest("req-1", {}, true, "auto", ""));
        QVERIFY(!model.acceptBrowserRequest("req-1", {QVariantMap{{"index", 0}, {"url", "https://example.invalid/a.zip"}, {"name", "../x"}}}, true, "auto", ""));
        QCOMPARE(service.commands, 0);
        service.commandResult = QJsonObject{{"ok", true}, {"id", "77"}, {"ids", QJsonArray{"77"}}};
        bool sent = false;
        QTRY_VERIFY(sent || (sent = model.acceptBrowserRequest("req-1", {QVariantMap{{"index", 1}, {"url", "https://example.invalid/b.zip"}, {"name", " b2.zip "}}}, false, "none", "/downloads")));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["op"].toString(), "browser.accept");
        QCOMPARE(service.lastCommand["requestId"].toString(), "req-1");
        QCOMPARE(service.lastCommand["folder"].toString(), "/downloads");
        QCOMPARE(service.lastCommand["start"].toBool(), false);
        const auto item = service.lastCommand["items"].toArray()[0].toObject();
        QCOMPARE(item["index"].toInt(), 1); QCOMPARE(item["name"].toString(), "b2.zip");
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.dismissBrowserRequest("req-1"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "browser.dismiss");
        QTRY_VERIFY(!model.busy());
        // A durable pre-Add reservation is visible but cannot be replayed, even by
        // invoking the model directly rather than using the disabled dialog button.
        QTRY_VERIFY(!model.busy());
        auto unknown = pending; unknown["outcomeUnknown"] = true;
        service.extensionSnapshot["browserRequests"] = QJsonArray{unknown};
        QTRY_VERIFY(model.browserRequests()[0].toMap()["outcomeUnknown"].toBool());
        const int beforeUnknown = service.commands;
        QVERIFY(!model.acceptBrowserRequest("req-1", {QVariantMap{{"index", 0}, {"url", "https://example.invalid/a.zip"}, {"name", "a.zip"}}}, true, "auto", ""));
        QCOMPARE(service.commands, beforeUnknown);
        QVERIFY(model.errorMessage().contains("unknown"));
        QVERIFY(model.updateBrowserSettings({{"apiEnabled", false}}));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "browser.settings");
        QCOMPARE(service.lastCommand["settings"].toObject()["apiEnabled"].toBool(), false);
        // Malformed browser data closes the connection and keeps the last coherent requests.
        auto bad = pending; bad["items"] = QJsonArray{QJsonObject{{"url", "x"}, {"name", ""}, {"hasHeaders", true}, {"downloadPage", QJsonValue::Null}}};
        service.extensionSnapshot["browserRequests"] = QJsonArray{bad};
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.browserRequests().size(), 1);
        service.extensionSnapshot["browserRequests"] = QJsonArray{};
        service.extensionSnapshot["browserIntegration"] = QJsonObject{{"state", "bogus"}, {"error", QJsonValue::Null}, {"enabled", true}, {"port", 15151}, {"auth", false}};
        QTest::qWait(600);
        QCOMPARE(model.browserIntegration()["state"].toString(), "listening");
        service.extensionSnapshot["browserIntegration"] = integration;
        QTRY_VERIFY(model.browserRequests().isEmpty());
    }
    void browserReviewDialogAndSettingsPageViaQml() {
        FakeService service; service.browserCapability = true; service.runtimeCapability = true; service.rows = {};
        const QJsonObject pending{{"id", "req-q"}, {"receivedAt", "1"}, {"items", QJsonArray{
            QJsonObject{{"url", "https://example.invalid/a.zip"}, {"name", "a.zip"}, {"hasHeaders", true}, {"downloadPage", QJsonValue::Null}},
            QJsonObject{{"url", "https://example.invalid/b.zip"}, {"name", "b.zip"}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}}}}};
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"browserRequests", QJsonArray{pending}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}},
            {"browserIntegration", QJsonObject{{"state", "failed"}, {"error", "Port 15151 is already in use by another application"}, {"enabled", true}, {"port", 15151}, {"auth", true}}},
            {"settings", QJsonObject{{"threadCount", 8}, {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3}, {"minPartSize", "2097152"},
                {"dynamicPartCreation", true}, {"useSparseFileAllocation", true}, {"useServerLastModifiedTime", false}}}};
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QObject *dialog = nullptr;
        QTRY_VERIFY((dialog = view.rootObject()->findChild<QObject *>("browserRequestDialog")) && dialog->property("opened").toBool());
        QCOMPARE(dialog->property("items").toList().size(), 2);
        auto *notice = view.rootObject()->findChild<QObject *>("downloadsStatusNotice");
        QVERIFY(notice && notice->property("text").toString().contains("1 link request waiting"));
        auto *accept = view.rootObject()->findChild<QObject *>("acceptBrowserRequest");
        QVERIFY(accept);
        QTRY_VERIFY(accept->property("enabled").toBool());
        const QString reviewCapture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!reviewCapture.isEmpty()) {
            QVERIFY(reviewWindow(qobject_cast<QQuickWindow *>(dialog), reviewCapture + "/browser-review.png", QSize(720, 580)));
            QVERIFY(reviewWindow(qobject_cast<QQuickWindow *>(dialog), reviewCapture + "/browser-review-narrow.png", QSize(400, 580)));
            for (const auto *name : {"acceptBrowserRequest", "pauseBrowserRequest", "dismissBrowserRequest", "laterBrowserRequest"}) {
                auto *button = dialog->findChild<QQuickItem *>(name); QVERIFY(button && button->isVisible());
                const auto bounds = button->mapRectToScene(QRectF(QPointF(), button->size()));
                QVERIFY2(QRectF(0, 0, 400, 580).contains(bounds), name);
            }
            view.setMinimumSize(QSize(0, 0)); view.setMaximumSize(QSize(16777215, 16777215)); view.resize(1000, 700);
        }
        QCOMPARE(service.commands, 0);
        service.commandResult = {{"ok", true}, {"ids", QJsonArray{"77", "79"}}, {"id", "77"}};
        QPointer<QObject> closedReview(dialog);
        QVERIFY(QMetaObject::invokeMethod(accept, "click"));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["op"].toString(), "browser.accept");
        QCOMPARE(service.lastCommand["start"].toBool(), true);
        QCOMPARE(service.lastCommand["items"].toArray().size(), 2);
        QCOMPARE(service.lastCommand["items"].toArray()[1].toObject()["url"].toString(), "https://example.invalid/b.zip");
        QCOMPARE(service.lastCommand["folder"].toString(), "/downloads");
        QTRY_VERIFY(!closedReview || !closedReview->property("visible").toBool());
        // The fake service still reports the request: explicit Review creates another
        // native toplevel. Closing it postpones, without any command.
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "reviewBrowserRequests"));
        QTRY_VERIFY((dialog = view.rootObject()->findChild<QObject *>("browserRequestDialog")) && dialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        service.extensionSnapshot["browserRequests"] = QJsonArray{};
        QTRY_VERIFY(model.browserRequests().isEmpty());
        QTRY_VERIFY(notice->property("text").toString().contains("already in use"));
        QCOMPARE(service.commands, 1);
        // Browser settings: port/enable apply; the stored key is never sent back unless typed.
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openSettings"));
        QObject *settings = nullptr;
        QTRY_VERIFY((settings = view.rootObject()->findChild<QObject *>("serviceSettingsDialog")) && settings->property("opened").toBool());
        settings->setProperty("page", 2);
        auto *port = view.rootObject()->findChild<QObject *>("settingsApiPort");
        auto *apply = view.rootObject()->findChild<QObject *>("applyServiceSettings");
        auto *status = view.rootObject()->findChild<QObject *>("browserIntegrationStatus");
        QVERIFY(port && apply && status);
        QVERIFY(status->property("text").toString().contains("already in use"));
        QCOMPARE(port->property("text").toString(), "15151");
        port->setProperty("text", "15152");
        QTRY_VERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_COMPARE(service.commands, 2);
        QCOMPARE(service.lastCommand["op"].toString(), "browser.settings");
        const auto patch = service.lastCommand["settings"].toObject();
        QCOMPARE(patch["apiPort"].toInt(), 15152); QVERIFY(patch["apiAuthEnabled"].toBool()); QVERIFY(!patch.contains("apiAuthKey"));
        port->setProperty("text", "70000");
        QVERIFY(!apply->property("enabled").toBool());
    }
    void desktopIntegrationActivationFocusAndExplicitClipboardCommands() {
        FakeService service; service.desktopCapability = true; service.runtimeCapability = true; service.browserCapability = true;
        service.extensionSnapshot = {{"queues", QJsonArray{}},
            {"browserRequests", QJsonArray{}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"error", QJsonValue::Null}, {"enabled", false}, {"port", 15151}, {"auth", false}}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/fixture/downloads"},
                {"destinationRoots", QJsonArray{"/fixture/downloads"}}, {"redirects", true}, {"proxy", "direct"}}},
            {"settings", QJsonObject{{"threadCount", 8}, {"maxConcurrentDownloads", 3}, {"maxDownloadRetryCount", 3}, {"minPartSize", "524288"},
                {"dynamicPartCreation", true}, {"useSparseFileAllocation", true}, {"useServerLastModifiedTime", false}}},
            {"integration", QJsonObject{{"autoStartOnBoot", false}, {"useSystemTray", true}, {"notificationSound", false}, {"notifications", true}}},
            {"frontend", QJsonObject{{"activation", QJsonValue::Null}}}};
        ServiceDownloads model(service.descriptor);
        QSignalSpy activation(&model, &ServiceDownloads::activationRequested);
        QSignalSpy clipboard(&model, &ServiceDownloads::clipboardImportRequested);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.integration().value("autoStartOnBoot").toBool(), false);
        model.setFrontendActive(true);
        QTRY_VERIFY(service.lastSnapshotRequest["active"].toBool());
        const QJsonObject request{{"serial", "1"}, {"action", "review"}, {"token", "fresh-token"}, {"downloadId", QJsonValue::Null}};
        service.extensionSnapshot["frontend"] = QJsonObject{{"activation", request}};
        QTRY_COMPARE(activation.count(), 1);
        QCOMPARE(activation.first()[0].toString(), "fresh-token");
        QTest::qWait(600); QCOMPARE(activation.count(), 1); QCOMPARE(service.commands, 0);
        model.importFromClipboard(); QCOMPARE(clipboard.count(), 1); QCOMPARE(service.commands, 0);
        QVERIFY(model.importLinks("https://example.invalid/fixture"));
        QTRY_COMPARE(service.commands, 1); QCOMPARE(service.lastCommand["op"].toString(), "links.import");
        QCOMPARE(service.lastCommand["text"].toString(), "https://example.invalid/fixture");
        QTRY_VERIFY(!model.busy());
        PreviewDownloads resources;
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openSettings"));
        auto *dialog = view.rootObject()->findChild<QObject *>("serviceSettingsDialog");
        auto *tray = view.rootObject()->findChild<QObject *>("settingsSystemTray");
        auto *apply = view.rootObject()->findChild<QObject *>("applyDesktopIntegration");
        QVERIFY(dialog && tray && apply); dialog->setProperty("page", 2);
        QTRY_VERIFY(apply->property("visible").toBool());
        tray->setProperty("checked", false);
        QTRY_VERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_COMPARE(service.commands, 2); QCOMPARE(service.lastCommand["op"].toString(), "settings.integration");
        const auto patch = service.lastCommand["settings"].toObject();
        QCOMPARE(patch.keys(), QStringList({"autoStartOnBoot", "notificationSound", "useSystemTray"}));
        QCOMPARE(patch["useSystemTray"], QJsonValue(false));
        QTRY_VERIFY(!model.busy());
        const QString capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QVERIFY(reviewFrame(view, capture + "/desktop-integration.png", QSize(1000, 700)));
            QVERIFY(reviewFrame(view, capture + "/desktop-integration-small.png", QSize(760, 600)));
        }
        // Invalid integration data must not partially replace the last coherent snapshot.
        service.extensionSnapshot["integration"] = QJsonObject{{"useSystemTray", "yes"}};
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QVERIFY(model.integration().value("useSystemTray").toBool());
        QCOMPARE(service.commands, 2);
    }
    // Real engine: reviewed batch -> Download -> automatic per-ID windows -> pause/resume ->
    // completion -> close/reopen -> real failure -> Retry from the window. Each mutating
    // control is clicked ONCE after waiting for the command gate; never polled clicks.
    void realEngineDownloadWindows() {
        const QString descriptor = qEnvironmentVariable("QT_SERVICE_TEST_DESCRIPTOR");
        if (descriptor.isEmpty()) QSKIP("Run scripts/qt-service/smoke.py for real-engine download-window coverage");
        const QString base = qEnvironmentVariable("QT_SERVICE_TEST_URL").section('/', 0, -2);
        const QString capture = qEnvironmentVariable("QT_SERVICE_TEST_CAPTURE_DIR");
        PreviewDownloads resources;
        ServiceDownloads model(descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 650); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(QTest::qWaitForWindowExposed(&view, 5000));
        QTRY_COMPARE(model.connectionState(), "ready");
        QVERIFY(model.capabilities().contains("download.windows"));
        QTRY_VERIFY(model.integration()["showDownloadProgressDialog"].toBool());
        QTest::qWait(500); QCOMPARE(windowCount(view), 0); // Earlier smoke events are a baseline.
        auto idFor = [&](const QString &name) {
            for (int i = 0; i < model.rowCount(); ++i)
                if (model.data(model.index(i), PreviewDownloads::FileName).toString() == name) return model.idAt(i);
            return QString();
        };
        // Slow, real limiter (not a fake progress source) so Pause lands mid-transfer.
        const QString previousLimit = model.settings()["speedLimit"].toString();
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.updateSettings({{"useSpeedLimit", "Enabled"}, {"speedLimit", "524288"}}));
        QTRY_COMPARE(model.settings()["speedLimit"].toString(), "524288");
        // The same durable review intake as the browser extension, entered explicitly.
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.importLinks(base + "/window-a.bin\n" + base + "/window-b.bin"));
        QObject *review = nullptr;
        QTRY_VERIFY((review = view.rootObject()->findChild<QObject *>("browserRequestDialog")) && review->property("opened").toBool());
        QCOMPARE(review->property("items").toList().size(), 2);
        if (!capture.isEmpty()) {
            QVERIFY(reviewWindow(qobject_cast<QQuickWindow *>(review), capture + "/qt-review-batch.png", QSize(720, 580)));
            QVERIFY(reviewWindow(qobject_cast<QQuickWindow *>(review), capture + "/qt-review-batch-small.png", QSize(400, 580)));
            view.setMinimumSize(QSize(0, 0)); view.setMaximumSize(QSize(16777215, 16777215)); view.resize(1000, 650);
        }
        auto *accept = view.rootObject()->findChild<QObject *>("acceptBrowserRequest");
        QVERIFY(accept); QTRY_VERIFY(accept->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
        QTRY_VERIFY2(!review->property("downloadId").toString().isEmpty(), modelState(model).constData());
        QVERIFY(review->property("visible").toBool()); // Same native surface, not a replacement popup.
        QString a, b;
        QTRY_VERIFY2(!(a = idFor("window-a.bin")).isEmpty() && !(b = idFor("window-b.bin")).isEmpty(), modelState(model).constData());
        QTRY_VERIFY(model.browserRequests().isEmpty());
        // A window opens for each accepted ID when the engine actually starts it.
        QTRY_VERIFY2_WITH_TIMEOUT(downloadWindow(view, a) != nullptr, modelState(model).constData(), 15000);
        auto *windowA = downloadWindow(view, a);
        QCOMPARE(windowA, qobject_cast<QQuickWindow *>(review));
        QVERIFY(model.capabilities().contains("download.parts"));
        QTRY_VERIFY_WITH_TIMEOUT(model.downloadParts(a).value("available").toBool(), 10000);
        QVERIFY(!model.downloadParts(a).value("items").toList().isEmpty());
        auto *pause = windowA->findChild<QObject *>("downloadWindowPause");
        auto *resume = windowA->findChild<QObject *>("downloadWindowResume");
        QVERIFY(pause && resume);
        QTRY_VERIFY2_WITH_TIMEOUT(model.downloadState(a)["state"].toString() == "downloading"
            && model.downloadState(a)["downloaded"].toString().toLongLong() > 65536, modelState(model).constData(), 15000);
        if (!capture.isEmpty()) {
            QVERIFY(reviewWindow(windowA, capture + "/qt-window-downloading.png", QSize(560, 560)));
            QVERIFY(reviewWindow(windowA, capture + "/qt-window-downloading-narrow.png", QSize(360, 560)));
        }
        QTRY_VERIFY(pause->property("enabled").toBool() && pause->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(pause, "clicked"));
        QTRY_VERIFY2_WITH_TIMEOUT(model.downloadState(a)["state"].toString() == "paused", modelState(model).constData(), 10000);
        const qint64 kept = model.downloadState(a)["downloaded"].toString().toLongLong();
        QVERIFY(kept > 0);
        if (!capture.isEmpty()) QVERIFY(reviewWindow(windowA, capture + "/qt-window-paused.png", QSize(560, 560)));
        // With A paused the manual queue starts B: its own window, A's stays put.
        QTRY_VERIFY2_WITH_TIMEOUT(downloadWindow(view, b) != nullptr, modelState(model).constData(), 15000);
        QCOMPARE(downloadWindow(view, a), windowA);
        QTRY_VERIFY(!model.busy());
        QTRY_VERIFY(resume->property("enabled").toBool() && resume->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(resume, "clicked"));
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.updateSettings({{"speedLimit", previousLimit}}));
        QTRY_VERIFY2_WITH_TIMEOUT(model.downloadState(a)["state"].toString() == "completed"
            && model.downloadState(b)["state"].toString() == "completed", modelState(model).constData(), 60000);
        // Resuming reused the existing window: still exactly one per ID.
        QCOMPARE(downloadWindow(view, a), windowA); QCOMPARE(windowCount(view), 2);
        auto *status = windowA->findChild<QObject *>("downloadWindowStatus");
        QVERIFY(status); QTRY_COMPARE(status->property("text").toString(), "Download complete");
        QVERIFY(model.errorMessage().isEmpty());
        if (!capture.isEmpty()) {
            QTest::qWait(500); // Let the separate completion page finish its natural shrink.
            QVERIFY(reviewWindow(windowA, capture + "/qt-window-completed.png", windowA->size()));
        }
        // Closing a window is local; reopening uses the stable ID from the list.
        QVERIFY(QMetaObject::invokeMethod(windowA, "close"));
        QTRY_VERIFY(!downloadWindow(view, a)); QCOMPARE(model.downloadState(a)["state"].toString(), "completed");
        model.setCategory("all"); model.setSearch({}); model.select(a);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openDownloadWindow"));
        QVERIFY(downloadWindow(view, a));
        // Real 404 -> failure collector -> window error -> explicit Retry in that window.
        QTRY_VERIFY(!model.busy());
        QVERIFY(model.addDownload(base + "/fixture?recovery=window", "window-failed.dat", true));
        QString failed;
        QTRY_VERIFY2(!(failed = idFor("window-failed.dat")).isEmpty(), modelState(model).constData());
        QTRY_VERIFY2_WITH_TIMEOUT(downloadWindow(view, failed) && model.downloadState(failed)["state"].toString() == "failed"
            && model.downloadState(failed)["error"].toMap().contains("title"), modelState(model).constData(), 15000);
        auto *failedWindow = downloadWindow(view, failed);
        auto *error = failedWindow->findChild<QObject *>("downloadWindowError");
        auto *retry = failedWindow->findChild<QObject *>("downloadWindowResume");
        QVERIFY(error && retry);
        QTRY_VERIFY(error->property("visible").toBool());
        QVERIFY(error->property("text").toString().contains("404"));
        if (!capture.isEmpty()) QVERIFY(reviewWindow(failedWindow, capture + "/qt-window-failed.png", QSize(560, 650)));
        QFile marker(qEnvironmentVariable("QT_SERVICE_TEST_RECOVERY_MARKER").replace("recover-qml", "recover-window"));
        QVERIFY(marker.open(QIODevice::WriteOnly)); marker.close();
        QTRY_VERIFY(!model.busy());
        QTRY_VERIFY(retry->property("enabled").toBool() && retry->property("text").toString() == "Retry");
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QTRY_VERIFY2_WITH_TIMEOUT(model.downloadState(failed)["state"].toString() == "completed", modelState(model).constData(), 30000);
        QCOMPARE(downloadWindow(view, failed), failedWindow);
        QVERIFY(model.errorMessage().isEmpty());
        if (!capture.isEmpty()) QVERIFY(reviewFrame(view, capture + "/qt-home-with-windows.png", QSize(1000, 650)));
    }
    void realEngineViaQml() {
        const QString descriptor = qEnvironmentVariable("QT_SERVICE_TEST_DESCRIPTOR");
        if (descriptor.isEmpty()) QSKIP("Run scripts/qt-service/smoke.py for real-engine adapter/QML coverage");
        PreviewDownloads resources;
        ServiceDownloads model(descriptor);
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml"));
        view.resize(1000, 650); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QTRY_COMPARE(model.connectionState(), "ready");
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
        auto *url = view.rootObject()->findChild<QObject *>("exampleUrlInput");
        auto *name = view.rootObject()->findChild<QObject *>("exampleNameInput");
        QVERIFY(url && name);
        auto *add = view.rootObject()->findChild<QObject *>("confirmAddExample");
        url->setProperty("text", "https://example.invalid/not-a-fixture");
        name->setProperty("text", "qt-client.bin");
        QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
        QVERIFY(view.rootObject()->property("addDialogVisible").toBool());
        QVERIFY(!model.errorMessage().isEmpty());
        url->setProperty("text", qEnvironmentVariable("QT_SERVICE_TEST_URL"));
        auto *openOptions = view.rootObject()->findChild<QObject *>("openAddHttpOptions");
        QVERIFY(openOptions); QTRY_VERIFY(openOptions->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(openOptions, "clicked"));
        auto *httpConnections = view.rootObject()->findChild<QObject *>("httpConnections");
        auto *applyOptions = view.rootObject()->findChild<QObject *>("applyHttpOptions");
        QVERIFY(httpConnections && applyOptions);
        httpConnections->setProperty("text", "2");
        const auto httpCaptureDir = qEnvironmentVariable("QT_SERVICE_TEST_CAPTURE_DIR");
        if (!httpCaptureDir.isEmpty()) {
            model.clearError();
            QVERIFY(reviewFrame(view, httpCaptureDir + "/qt-http-options.png", QSize(1000, 650)));
            QVERIFY(reviewFrame(view, httpCaptureDir + "/qt-http-options-small.png", QSize(760, 600)));
            auto *tabs = view.rootObject()->findChild<QObject *>("httpOptionsTabs");
            QVERIFY(tabs);
            tabs->setProperty("currentIndex", 1);
            QVERIFY(reviewFrame(view, httpCaptureDir + "/qt-http-authentication-small.png", QSize(760, 600)));
            tabs->setProperty("currentIndex", 2);
            QVERIFY(reviewFrame(view, httpCaptureDir + "/qt-http-headers-small.png", QSize(760, 600)));
            tabs->setProperty("currentIndex", 0);
        }
        QTRY_VERIFY(applyOptions->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(applyOptions, "clicked"));
        // Wait for the command gate, then click ONCE. Polling a mutating click can
        // duplicate an Add if native dialog-close delivery lags behind the reply.
        QTRY_VERIFY(!model.busy());
        QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
        // The fixture URL already belongs to earlier smoke downloads: review it explicitly.
        auto *duplicate = view.rootObject()->findChild<QObject *>("duplicateDownloadDialog");
        QVERIFY(duplicate);
        QTRY_VERIFY(duplicate->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject()->findChild<QObject *>("duplicateAddNew"), "clicked"));
        QTRY_VERIFY2(!view.rootObject()->property("addDialogVisible").toBool(), modelState(model).constData());
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            for (int i = 0; i < model.rowCount(); ++i)
                if (model.data(model.index(i), PreviewDownloads::FileName).toString() == "qt-client.bin") { model.select(model.idAt(i)); return true; }
            return false;
        })(), 5000);
        // Edit captures the stable target, changes only source/options, and stays paused.
        QTRY_VERIFY(!model.busy());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openHttpEdit"));
        httpConnections->setProperty("text", "3");
        QTRY_VERIFY(applyOptions->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(applyOptions, "clicked"));
        QTRY_COMPARE(model.downloadDetails(model.selectedId())["details"].toMap()["connections"].toInt(), 3);
        bool resumed = false;
        QTRY_VERIFY(resumed || (resumed = model.resumeSelected()));
        const QString selectedId = model.selectedId();
        QTRY_VERIFY2_WITH_TIMEOUT(([&] {
            for (int i = 0; i < model.rowCount(); ++i)
                if (model.idAt(i) == selectedId)
                    return model.data(model.index(i), PreviewDownloads::TransferState).toString() == "completed";
            return false;
        })(), modelState(model).constData(), 30000);
        QVERIFY(model.errorMessage().isEmpty());
        // The same authenticated adapter supplies persisted settings to the original-style form.
        QTRY_VERIFY(!model.busy());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openSettings"));
        auto *threadField = view.rootObject()->findChild<QObject *>("settingsThreadCount");
        auto *settingsApply = view.rootObject()->findChild<QObject *>("applyServiceSettings");
        QVERIFY(threadField && settingsApply);
        threadField->setProperty("text", "3");
        auto *speedMode = view.rootObject()->findChild<QObject *>("settingsSpeedMode");
        auto *speedLimit = view.rootObject()->findChild<QObject *>("settingsSpeedLimit");
        auto *averageSpeed = view.rootObject()->findChild<QObject *>("settingsAverageSpeed");
        QVERIFY(speedMode && speedLimit && averageSpeed);
        speedMode->setProperty("currentIndex", 1); speedLimit->setProperty("text", "4194304"); averageSpeed->setProperty("checked", false);
        QTRY_VERIFY(settingsApply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(settingsApply, "clicked"));
        QTRY_COMPARE(model.settings().value("threadCount").toInt(), 3);
        QCOMPARE(model.settings()["useSpeedLimit"].toString(), "Enabled");
        QCOMPARE(model.settings()["speedLimit"].toString(), "4194304");
        QCOMPARE(model.settings()["useAverageSpeed"].toBool(), false);
        const auto captureDir = qEnvironmentVariable("QT_SERVICE_TEST_CAPTURE_DIR");
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-settings.png", QSize(1000, 650))); }
        auto *settingsDialog = view.rootObject()->findChild<QObject *>("serviceSettingsDialog");
        QVERIFY(settingsDialog);
        if (!captureDir.isEmpty()) {
            settingsDialog->setProperty("page", 0);
            QVERIFY(reviewFrame(view, captureDir + "/qt-appearance.png", QSize(1000, 650)));
            settingsDialog->setProperty("page", 2);
            QVERIFY(reviewFrame(view, captureDir + "/qt-integrations.png", QSize(1000, 650)));
            settingsDialog->setProperty("page", 1);
        }
        QVERIFY(QMetaObject::invokeMethod(settingsDialog, "close"));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openQueues", Q_ARG(QString, "0")));
        auto *newQueue = view.rootObject()->findChild<QObject *>("newQueueName");
        auto *createQueue = view.rootObject()->findChild<QObject *>("createServiceQueue");
        QVERIFY(newQueue && createQueue);
        newQueue->setProperty("text", "Qt queue");
        QTRY_VERIFY(createQueue->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(createQueue, "clicked"));
        QTRY_VERIFY(model.queues().size() >= 2);
        QString queueId;
        for (const auto &entry : model.queues()) if (entry.toMap().value("name") == "Qt queue") queueId = entry.toMap().value("id").toString();
        QVERIFY(!queueId.isEmpty());
        auto *queuesDialog = view.rootObject()->findChild<QObject *>("serviceQueuesDialog");
        QVERIFY(queuesDialog); QVERIFY(QMetaObject::invokeMethod(queuesDialog, "choose", Q_ARG(QString, queueId)));
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-queues.png", QSize(1000, 650))); }
        auto *scheduleStart = view.rootObject()->findChild<QObject *>("queueScheduleStart");
        auto *startTime = view.rootObject()->findChild<QObject *>("queueStartTime");
        auto *applySchedule = view.rootObject()->findChild<QObject *>("applyQueueSchedule");
        QVERIFY(scheduleStart && startTime && applySchedule);
        scheduleStart->setProperty("checked", true);
        startTime->setProperty("text", QTime::currentTime().addSecs(3600).toString("HH:mm"));
        QTRY_VERIFY(applySchedule->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(applySchedule, "clicked"));
        QTRY_VERIFY(([&] {
            for (const auto &entry : model.queues()) if (entry.toMap().value("id").toString() == queueId)
                return entry.toMap().value("schedule").toMap().value("enabledStartTime").toBool();
            return false;
        })());
        view.resize(760, 600);
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-queues-small.png", QSize(760, 600))); }
        QVERIFY(QMetaObject::invokeMethod(queuesDialog, "close"));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openSettings"));
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-settings-small.png", QSize(760, 600))); }
        QVERIFY(QMetaObject::invokeMethod(settingsDialog, "close"));
        view.resize(1000, 650);
        // Original category fields drive the extracted CategoryManager, not extension guesses.
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openCategories"));
        auto *categoryDialog = view.rootObject()->findChild<QObject *>("serviceCategoriesDialog");
        auto *categoryName = view.rootObject()->findChild<QObject *>("categoryName");
        auto *categoryTypes = view.rootObject()->findChild<QObject *>("categoryTypes");
        auto *categoryApply = view.rootObject()->findChild<QObject *>("applyServiceCategory");
        QVERIFY(categoryDialog && categoryName && categoryTypes && categoryApply);
        categoryName->setProperty("text", "Qt fixtures"); categoryTypes->setProperty("text", "bin");
        QTRY_VERIFY(categoryApply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(categoryApply, "clicked"));
        QString categoryId;
        QTRY_VERIFY(([&] {
            for (const auto &entry : model.categories()) if (entry.toMap()["name"].toString() == "Qt fixtures") { categoryId = entry.toMap()["id"].toString(); return true; }
            return false;
        })());
        QVERIFY(QMetaObject::invokeMethod(categoryDialog, "choose", Q_ARG(QString, categoryId)));
        categoryName->setProperty("text", "Draft survives polling");
        QTest::qWait(600); QCOMPARE(categoryName->property("text").toString(), "Draft survives polling");
        categoryName->setProperty("text", "Qt fixtures");
        auto *assign = view.rootObject()->findChild<QObject *>("assignSelectedCategory");
        QVERIFY(assign); QTRY_VERIFY(assign->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(assign, "clicked"));
        QTRY_VERIFY(([&] {
            for (const auto &entry : model.categories()) if (entry.toMap()["id"].toString() == categoryId)
                return entry.toMap()["items"].toList().contains(selectedId);
            return false;
        })());
        model.setCategory("category:" + categoryId); QCOMPARE(model.rowCount(), 1);
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-categories.png", QSize(1000, 650))); }
        view.resize(760, 600);
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-categories-small.png", QSize(760, 600))); }
        QVERIFY(QMetaObject::invokeMethod(categoryDialog, "close"));
        view.resize(1000, 650); model.setCategory("all");
        // The original Add/Download distinction is real, not a UI-only toggle.
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
        url->setProperty("text", qEnvironmentVariable("QT_SERVICE_TEST_URL"));
        name->setProperty("text", "qt-direct.bin");
        auto *categorySelection = view.rootObject()->findChild<QObject *>("addCategorySelection");
        QVERIFY(categorySelection);
        // Auto + None precede the persisted categories. Last is the newly-created one.
        categorySelection->setProperty("currentIndex", model.categories().size() + 1);
        auto *start = view.rootObject()->findChild<QObject *>("confirmStartDownload");
        QVERIFY(start && start->property("visible").toBool());
        QTRY_VERIFY(!model.busy());
        QVERIFY(QMetaObject::invokeMethod(start, "clicked"));
        QTRY_VERIFY(duplicate->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject()->findChild<QObject *>("duplicateAddNew"), "clicked"));
        QTRY_VERIFY2(!view.rootObject()->property("addDialogVisible").toBool(), modelState(model).constData());
        QTRY_VERIFY2_WITH_TIMEOUT(([&] {
            for (int i = 0; i < model.rowCount(); ++i)
                if (model.data(model.index(i), PreviewDownloads::FileName).toString() == "qt-direct.bin")
                    return model.data(model.index(i), PreviewDownloads::TransferState).toString() == "completed";
            return false;
        })(), modelState(model).constData(), 30000);
        // Save one more download, attach through the QML Items page, start through its footer.
        bool queuedAdd = false;
        QTRY_VERIFY(queuedAdd || (queuedAdd = model.addDownload(qEnvironmentVariable("QT_SERVICE_TEST_URL"), "qt-queued.bin", false)));
        QTRY_COMPARE(model.selectedName(), "qt-queued.bin");
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openQueues", Q_ARG(QString, queueId)));
        queuesDialog->setProperty("page", 1);
        auto *addSelected = view.rootObject()->findChild<QObject *>("addSelectedToQueue");
        QVERIFY(addSelected); QTRY_VERIFY(addSelected->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(addSelected, "clicked"));
        QTRY_VERIFY(([&] {
            for (const auto &entry : model.queues()) if (entry.toMap().value("id").toString() == queueId)
                return entry.toMap().value("items").toList().contains(model.selectedId());
            return false;
        })());
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-queue-items-small.png", QSize(760, 600))); }
        auto *toggleQueue = view.rootObject()->findChild<QObject *>("toggleServiceQueue");
        QVERIFY(toggleQueue); QTRY_VERIFY(toggleQueue->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(toggleQueue, "clicked"));
        QTRY_VERIFY2_WITH_TIMEOUT(([&] {
            for (int i = 0; i < model.rowCount(); ++i)
                if (model.data(model.index(i), PreviewDownloads::FileName).toString() == "qt-queued.bin")
                    return model.data(model.index(i), PreviewDownloads::TransferState).toString() == "completed";
            return false;
        })(), modelState(model).constData(), 30000);
        QVERIFY(model.errorMessage().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(queuesDialog, "close"));
        // Real core error -> original failure collector -> Qt inspection -> explicit Retry.
        bool failedAdd = false;
        QTRY_VERIFY(failedAdd || (failedAdd = model.addDownload(qEnvironmentVariable("QT_SERVICE_TEST_URL") + "?recovery=qml", "qt-recovery.dat", true)));
        QTRY_COMPARE(model.selectedName(), "qt-recovery.dat");
        const auto failedId = model.selectedId();
        QTRY_VERIFY_WITH_TIMEOUT(model.downloadDetails(failedId)["details"].toMap()["error"].toMap().contains("title"), 15000);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openDetails"));
        auto *detailsDialog = view.rootObject()->findChild<QObject *>("downloadDetailsDialog");
        auto *description = view.rootObject()->findChild<QObject *>("downloadErrorDescription");
        auto *retry = view.rootObject()->findChild<QObject *>("retryDownloadDetails");
        QVERIFY(detailsDialog && description && retry);
        QVERIFY(description->property("text").toString().contains("404"));
        QCOMPARE(detailsDialog->property("targetId").toString(), failedId);
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-error.png", QSize(1000, 650))); }
        view.resize(760, 600);
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-error-small.png", QSize(760, 600))); }
        QFile marker(qEnvironmentVariable("QT_SERVICE_TEST_RECOVERY_MARKER"));
        QVERIFY(marker.open(QIODevice::WriteOnly)); marker.close();
        QTRY_VERIFY(retry->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(retry, "clicked"));
        QTRY_VERIFY2_WITH_TIMEOUT(model.downloadDetails(failedId)["state"].toString() == "completed", modelState(model).constData(), 30000);
        QVERIFY(model.downloadDetails(failedId)["details"].toMap()["error"].isNull());
        QVERIFY(model.errorMessage().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(detailsDialog, "close"));
        model.select(failedId); QTRY_VERIFY(model.canRemove());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
        auto *removeDialog = view.rootObject()->findChild<QObject *>("removeExampleDialog");
        auto *remove = view.rootObject()->findChild<QObject *>("confirmRemoveExample");
        QVERIFY(removeDialog && remove);
        QCOMPARE(removeDialog->property("targetId").toString(), failedId);
        view.resize(1000, 650);
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-remove.png", QSize(1000, 650))); }
        view.resize(760, 600);
        if (!captureDir.isEmpty()) { QVERIFY(reviewFrame(view, captureDir + "/qt-remove-small.png", QSize(760, 600))); }
        model.select(selectedId); // Confirmation must still remove the captured target.
        QTRY_VERIFY(remove->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
        QTRY_VERIFY(model.downloadDetails(failedId).isEmpty());
        QCOMPARE(model.selectedId(), selectedId);
        QVERIFY(model.errorMessage().isEmpty());
    }
};
QTEST_MAIN(ServiceDownloadsTest)
#include "tst_servicedownloads.moc"
