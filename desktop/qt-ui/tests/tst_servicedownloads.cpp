#include "servicedownloads.h"
#include "downloadpartslots.h"
#include "previewdownloads.h"
#include "renderwait.h"
#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QMimeData>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtMath>
#include <QtTest>
#include <memory>
#include <QSignalSpy>
#include <QClipboard>
#include <QApplication>
#include <QProcess>
#include <QThread>
#include <vector>

// The fake service's handshake can exceed QTRY's 5 s default under `ctest -j` load.
#define QTRY_READY(model) QTRY_COMPARE_WITH_TIMEOUT((model).connectionState(), QStringLiteral("ready"), 20000)

static QByteArray encode(const QJsonObject &object) {
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray result(4, Qt::Uninitialized);
    qToBigEndian<quint32>(bytes.size(), result.data()); result += bytes; return result;
}

static QQuickItem *visualChild(QQuickItem *root, const QString &name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *found = visualChild(child, name)) return found;
    return nullptr;
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
    bool policyCapability = false, categoryDestinationCapability = false, checksumCapability = false;
    bool categoryCapability = false, monitorCapability = false, detailsCapability = false, forgetCapability = false, runtimeCapability = false, httpCapability = false;
    bool browserCapability = false, desktopCapability = false, windowsCapability = false;
    // New negotiated settings/duplicate extensions.
    bool folderCapability = false, duplicateCapability = false, replaceCapability = false;
    bool actionsCapability = false, sortCapability = false, tasksCapability = false, reviewCapability = false, probeBatchCapability = false, categoryIconCapability = false, soundCapability = false, powerCapability = false;
    // Sets one review row's browser.probeBatch outcome in the next snapshots.
    void setProbe(int index, const QJsonObject &probe) {
        auto requests = extensionSnapshot["browserRequests"].toArray(); auto request = requests[0].toObject();
        auto items = request["items"].toArray(); auto item = items[index].toObject(); item["probe"] = probe; items[index] = item;
        request["items"] = items; requests[0] = request; extensionSnapshot["browserRequests"] = requests;
    }
    QJsonArray reviewItems;
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
                    if (forgetCapability) { caps.append("download.forget"); caps.append("download.delete"); }
                    if (runtimeCapability) caps.append("runtime.policy");
                    if (httpCapability) { caps.append("download.edit"); caps.append("download.http-options"); }
                    if (browserCapability) for (const auto &op : {"browser.integration", "browser.intake", "browser.accept", "browser.dismiss", "browser.settings"}) caps.append(op);
                    if (desktopCapability) for (const auto &op : {"settings.integration", "frontend.activate", "links.import", "service.shutdown"}) caps.append(op);
                    if (windowsCapability) caps.append("download.windows");
                    if (partsCapability) caps.append("download.parts");
                    if (folderCapability) caps.append("settings.folder");
                    if (duplicateCapability) caps.append("settings.duplicates");
                    if (replaceCapability) caps.append("download.replace");
                    if (sortCapability) caps.append("settings.sort");
                    if (reviewCapability) { caps.append("links.review"); caps.append("browser.probe"); }
                    if (probeBatchCapability) caps.append("browser.probeBatch");
                    if (categoryIconCapability) caps.append("category.icon");
                    if (categoryDestinationCapability) caps.append("category.destination");
                    if (checksumCapability) caps.append("download.checksum");
                    if (soundCapability) caps.append("settings.notificationSounds");
                    if (powerCapability) for (const auto &op : {"queue.update", "queue.powerAction", "power.execute", "power.cancel"}) caps.append(op);
                    if (tasksCapability) for (const auto &op : {"queue.startAll", "queue.stopAll", "download.stopAll"}) caps.append(op);
                    if (actionsCapability) for (const auto &op : {"download.reset", "download.move", "download.links"}) caps.append(op);
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
                    if (sortCapability && request["op"] == "settings.update" && !refused) {
                        auto settings = extensionSnapshot["settings"].toObject();
                        const auto patch = request["settings"].toObject();
                        for (auto i = patch.begin(); i != patch.end(); ++i) settings[i.key()] = i.value();
                        extensionSnapshot["settings"] = settings;
                    }
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
                    if (reviewCapability && request["op"] == "links.review") {
                        extensionSnapshot["browserRequests"] = QJsonArray{QJsonObject{{"id", "manual-review"}, {"receivedAt", "1"}, {"source", "clipboard"}, {"outcomeUnknown", false}, {"items", reviewItems}}};
                        reply["result"] = QJsonObject{{"ok", true}, {"requestId", "manual-review"}};
                    }
                    if (probeBatchCapability && request["op"] == "browser.probeBatch") {
                        for (const auto &index : request["indices"].toArray()) setProbe(index.toInt(), QJsonObject{{"state", "pending"}});
                        reply["result"] = QJsonObject{{"ok", true}, {"requestId", request["requestId"]}, {"queued", request["indices"].toArray().size()}};
                    }
                    if (reviewCapability && request["op"] == "browser.probe")
                        reply["result"] = QJsonObject{{"ok", true}, {"requestId", request["requestId"]}, {"index", request["index"]}, {"statusCode", 206}, {"size", "3145728"}};
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
    void initTestCase() {
        QGuiApplication::setDesktopFileName(QStringLiteral("org.abdownloadmanager.QtCapability.ServiceTest-%1").arg(QCoreApplication::applicationPid()));
        QQuickStyle::setStyle("org.kde.desktop"); QQuickWindow::setDefaultAlphaBuffer(true);
    }
    void multipleAddReusesSearchableReviewWithStableIndicesAndMetadata() {
        FakeService service; service.reviewCapability = service.browserCapability = service.runtimeCapability = service.fulfillBrowserAccept = true;
        service.rows = {};
        for (const auto &name : {"alpha.zip", "beta.zip", "gamma.zip"}) service.reviewItems.append(QJsonObject{
            {"url", "https://example.invalid/" + QString(name)}, {"name", name}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}});
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()}, {"browserRequests", QJsonArray{}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"error", QJsonValue::Null}, {"enabled", false}, {"port", 15151}, {"auth", false}}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}}};
        ServiceDownloads model(service.descriptor); QTRY_READY(model);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1000, 700);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.show(); QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMultipleAdd"));
        auto *entry = view.rootObject()->findChild<QObject *>("multipleAddLinks"); QVERIFY(entry);
        entry->setProperty("text", "https://example.invalid/alpha.zip\nhttps://example.invalid/beta.zip\nhttps://example.invalid/gamma.zip");
        const int hold = qBound(0, qEnvironmentVariableIntValue("QT_MULTIPLE_GALLERY_HOLD_MS"), 10000); if (hold) QTest::qWait(hold);
        auto *reviewButton = view.rootObject()->findChild<QObject *>("reviewMultipleLinks"); QVERIFY(reviewButton);
        QVERIFY(QMetaObject::invokeMethod(reviewButton, "clicked"));
        QObject *review = nullptr;
        QTRY_VERIFY((review = view.rootObject()->findChild<QObject *>("browserRequestDialog")) && review->property("visible").toBool());
        QCOMPARE(service.commandLog.first()["op"].toString(), "links.review"); QCOMPARE(service.commandLog.size(), 1);
        QCOMPARE(review->property("title").toString(), "Add Multiple Downloads");
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QVERIFY(reviewWindow(qobject_cast<QQuickWindow *>(review), capture + "/review-narrow.png", QSize(400, 520)));
            QVERIFY(reviewWindow(qobject_cast<QQuickWindow *>(review), capture + "/review-client.png", QSize(720, 520)));
        }
        if (hold) { view.hide(); QTest::qWait(hold); }
        auto *search = review->findChild<QObject *>("browserRequestSearch"); QVERIFY(search); search->setProperty("text", "beta");
        auto *list = review->findChild<QObject *>("browserRequestItems"); QVERIFY(list); QTRY_COMPARE(list->property("count").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(review, "setAll", Q_ARG(bool, false)));
        QObject *check = nullptr; QTRY_VERIFY((check = visualChild(qobject_cast<QQuickItem *>(list), "browserRequestChecked-1")));
        check->setProperty("checked", true); QVERIFY(QMetaObject::invokeMethod(check, "toggled"));
        QTRY_COMPARE(review->property("checkedCount").toInt(), 1);
        auto *name = visualChild(qobject_cast<QQuickItem *>(list), "browserRequestName-1"); QVERIFY(name);
        name->setProperty("text", "renamed-beta.zip"); QVERIFY(QMetaObject::invokeMethod(name, "textEdited"));
        QTRY_VERIFY(!model.busy());
        auto *probe = review->findChild<QObject *>("probeBrowserRequestSizes"); QVERIFY(probe); QVERIFY(QMetaObject::invokeMethod(probe, "clicked"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "browser.probe"); QCOMPARE(service.lastCommand["index"].toInt(), 1);
        auto *size = visualChild(qobject_cast<QQuickItem *>(list), "browserRequestSize-1"); QVERIFY(size); QTRY_COMPARE(size->property("text").toString(), "3 MiB");
        auto *location = review->findChild<QObject *>("browserRequestLocation"); QVERIFY(location); location->setProperty("text", "/downloads/nested");
        // A folder picker sits beside the field; it is never clicked here (that would reach a real session bus).
        QVERIFY(review->findChild<QObject *>("browserRequestLocationBrowse"));
        auto *category = review->findChild<QObject *>("browserRequestCategory"); QVERIFY(category); category->setProperty("currentIndex", 1);
        QTRY_VERIFY(!model.busy()); if (hold) QTest::qWait(hold);
        auto *accept = review->findChild<QObject *>("pauseBrowserRequest"); QVERIFY(accept); QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "browser.accept");
        const auto chosen = service.lastCommand["items"].toArray(); QCOMPARE(chosen.size(), 1); QCOMPARE(chosen[0].toObject()["index"].toInt(), 1);
        QCOMPARE(chosen[0].toObject()["name"].toString(), "renamed-beta.zip"); QCOMPARE(service.lastCommand["folder"].toString(), "/downloads/nested");
        QCOMPARE(service.lastCommand["categoryId"].toString(), "none"); QVERIFY(!service.lastCommand["start"].toBool());
        QTRY_COMPARE(model.rowCount(), 1); view.close(); QTest::qWait(300);
        view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(100);
    }
    void multipleAddExplainsOversizedTextAndClipboardFollowsRuntime() {
        // New Download from Clipboard is shown in the installed (explicit-runtime) mode only.
        for (const QString mode : {"explicit-runtime", "isolated"}) {
            FakeService service; service.reviewCapability = service.browserCapability = service.runtimeCapability = service.desktopCapability = true;
            service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()}, {"browserRequests", QJsonArray{}},
                {"browserIntegration", QJsonObject{{"state", "disabled"}, {"error", QJsonValue::Null}, {"enabled", false}, {"port", 15151}, {"auth", false}}},
                {"integration", QJsonObject{{"autoStartOnBoot", false}, {"useSystemTray", false}, {"notificationSound", false}, {"notifications", true}}},
                {"runtime", QJsonObject{{"mode", mode}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}}};
            ServiceDownloads model(service.descriptor); QTRY_READY(model);
            QTRY_COMPARE(model.runtime()["mode"].toString(), mode);
            QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1000, 700);
            view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
            view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.show(); QCOMPARE(view.status(), QQuickView::Ready);
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMenu", Q_ARG(QString, "File")));
            auto *fileMenu = view.rootObject()->findChild<QObject *>("fileMenu"); QVERIFY(fileMenu); QTRY_VERIFY(fileMenu->property("visible").toBool());
            auto *clipboard = view.rootObject()->findChild<QObject *>("importClipboardLinks"); QVERIFY(clipboard);
            QTRY_COMPARE(clipboard->property("visible").toBool(), mode == "explicit-runtime");
            QVERIFY(QMetaObject::invokeMethod(fileMenu, "close")); QTRY_VERIFY(!fileMenu->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMultipleAdd"));
            auto *entry = view.rootObject()->findChild<QObject *>("multipleAddLinks"); QVERIFY(entry);
            auto *warning = view.rootObject()->findChild<QObject *>("multipleAddTooLarge"); QVERIFY(warning);
            auto *review = view.rootObject()->findChild<QObject *>("reviewMultipleLinks"); QVERIFY(review);
            const QString link = "https://example.invalid/a.zip\n";
            entry->setProperty("text", link.repeated(256 * 1024 / link.size()));
            QTRY_VERIFY(review->property("enabled").toBool());
            QVERIFY(!warning->property("visible").toBool());
            entry->setProperty("text", link.repeated(256 * 1024 / link.size() + 1));
            QTRY_VERIFY(warning->property("visible").toBool());
            QVERIFY(warning->property("message").toString().contains("256 KB"));
            QVERIFY(!review->property("enabled").toBool());
            QVERIFY(service.commandLog.isEmpty());
            view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    void checkSizesRunsInTheServiceWithoutHoldingTheCommandSlot() {
        FakeService service; service.reviewCapability = service.probeBatchCapability = service.browserCapability = service.runtimeCapability = true;
        for (const auto &name : {"alpha.zip", "beta.zip", "gamma.zip"}) service.reviewItems.append(QJsonObject{
            {"url", "https://example.invalid/" + QString(name)}, {"name", name}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}});
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()}, {"browserRequests", QJsonArray{}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"error", QJsonValue::Null}, {"enabled", false}, {"port", 15151}, {"auth", false}}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}}};
        ServiceDownloads model(service.descriptor); QTRY_READY(model);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1000, 700);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.show(); QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMultipleAdd"));
        view.rootObject()->findChild<QObject *>("multipleAddLinks")->setProperty("text", "https://example.invalid/alpha.zip\nhttps://example.invalid/beta.zip\nhttps://example.invalid/gamma.zip");
        QVERIFY(QMetaObject::invokeMethod(view.rootObject()->findChild<QObject *>("reviewMultipleLinks"), "clicked"));
        QObject *review = nullptr;
        QTRY_VERIFY((review = view.rootObject()->findChild<QObject *>("browserRequestDialog")) && review->property("visible").toBool());
        auto *list = qobject_cast<QQuickItem *>(review->findChild<QObject *>("browserRequestItems")); QVERIFY(list);
        QTRY_VERIFY(!model.busy());
        auto *probe = review->findChild<QObject *>("probeBrowserRequestSizes"); QVERIFY(probe);
        QTRY_VERIFY(probe->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(probe, "clicked"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "browser.probeBatch");
        QCOMPARE(service.lastCommand["indices"].toArray(), (QJsonArray{0, 1, 2}));
        // Probes are running in the service, yet the frontend is free for other commands.
        QTRY_VERIFY(!model.busy());
        QObject *size = nullptr;
        QTRY_VERIFY((size = visualChild(list, "browserRequestSize-1")) && size->property("text").toString() == "Checking…");
        QVERIFY(!probe->property("enabled").toBool());
        QVERIFY(model.pauseDownload("9007199254740993"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "pause");
        QTRY_VERIFY(!model.busy());
        service.setProbe(0, {{"state", "done"}, {"size", "3145728"}, {"statusCode", 206}, {"requiresAuth", false}});
        service.setProbe(1, {{"state", "done"}, {"size", "100"}, {"statusCode", 401}, {"requiresAuth", true}});
        service.setProbe(2, {{"state", "failed"}});
        QTRY_COMPARE(visualChild(list, "browserRequestSize-0")->property("text").toString(), "3 MiB");
        QTRY_COMPARE(visualChild(list, "browserRequestSize-1")->property("text").toString(), "Login required");
        QTRY_COMPARE(visualChild(list, "browserRequestSize-2")->property("text").toString(), "Unavailable");
        QTRY_VERIFY(probe->property("enabled").toBool());
        int probes = 0;
        for (const auto &command : std::as_const(service.commandLog)) probes += command["op"].toString().startsWith("browser.probe");
        QCOMPARE(probes, 1);
        // Malformed probe results are rejected like any other snapshot field.
        service.setProbe(0, {{"state", "done"}, {"size", "-7"}, {"statusCode", 206}, {"requiresAuth", false}});
        QTRY_COMPARE(model.errorMessage(), "Invalid service response. Connection closed.");
        view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void exitConfirmsOnlyWhileDownloadsOrQueuesRun() {
        FakeService service; service.desktopCapability = true; // Default row is downloading.
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()},
            {"integration", QJsonObject{{"autoStartOnBoot", false}, {"useSystemTray", false}, {"notificationSound", false}, {"notifications", true}}}};
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 1);
        QSignalSpy stopped(&model, &ServiceDownloads::serviceStopped);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1000, 700);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.show(); QCOMPARE(view.status(), QQuickView::Ready);
        auto *root = view.rootObject();
        QVERIFY(QMetaObject::invokeMethod(root, "openMenu", Q_ARG(QString, "File")));
        auto *fileMenu = root->findChild<QObject *>("fileMenu"); QTRY_VERIFY(fileMenu->property("visible").toBool());
        auto *exit = root->findChild<QObject *>("exitApplication"); QVERIFY(exit);
        QTRY_VERIFY(exit->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(exit, "triggered"));
        auto *dialog = root->findChild<QObject *>("confirmExitDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QVERIFY(root->property("mainModalOpen").toBool());
        QTest::qWait(100); QCOMPARE(service.commands, 0); // Nothing stops before confirmation.
        QVERIFY(QMetaObject::invokeMethod(dialog, "close")); QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(service.commands, 0);
        QVERIFY(QMetaObject::invokeMethod(root, "requestExit")); QTRY_VERIFY(dialog->property("visible").toBool());
        auto *confirm = root->findChild<QObject *>("confirmExit"); QVERIFY(confirm);
        QTRY_VERIFY(confirm->property("highlighted").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "service.shutdown");
        QTRY_COMPARE(stopped.count(), 1); QTRY_VERIFY(!dialog->property("visible").toBool());
        // Nothing running: exit stops the service at once.
        auto row = service.rows[0].toObject(); row["state"] = "paused"; service.rows[0] = row;
        QTRY_COMPARE(model.statistics()["activeCount"].toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(root, "requestExit"));
        QTRY_COMPARE(service.commands, 2); QCOMPARE(service.lastCommand["op"].toString(), "service.shutdown");
        QVERIFY(!dialog->property("visible").toBool());
        view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void droppedLinksOpenTheSharedReview() {
        FakeService service; service.reviewCapability = service.browserCapability = service.runtimeCapability = true;
        for (const auto &name : {"alpha.zip", "beta.zip"}) service.reviewItems.append(QJsonObject{
            {"url", "https://example.invalid/" + QString(name)}, {"name", name}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}});
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()}, {"browserRequests", QJsonArray{}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"error", QJsonValue::Null}, {"enabled", false}, {"port", 15151}, {"auth", false}}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}}};
        ServiceDownloads model(service.descriptor); QTRY_READY(model);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1000, 700);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.show(); QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *overlay = qobject_cast<QQuickItem *>(view.rootObject()->findChild<QObject *>("linkDropOverlay")); QVERIFY(overlay);
        const QPoint at(500, 350);
        const auto drag = [&](QMimeData *data) {
            QDragEnterEvent enter(at, Qt::CopyAction | Qt::MoveAction, data, Qt::LeftButton, Qt::NoModifier);
            QGuiApplication::sendEvent(&view, &enter);
            return enter.isAccepted();
        };
        const auto leave = [&] { QDragLeaveEvent left; QGuiApplication::sendEvent(&view, &left); };
        // Local files are not links: refused, nothing is read or sent.
        QMimeData files; files.setUrls({QUrl::fromLocalFile("/tmp/list.txt")});
        QVERIFY(!drag(&files)); QVERIFY(!overlay->isVisible()); leave();
        QMimeData links; links.setUrls({QUrl("https://example.invalid/alpha.zip"), QUrl("file:///tmp/x"), QUrl("https://example.invalid/beta.zip")});
        links.setText("ignored when URLs are present");
        QVERIFY(drag(&links));
        QTRY_VERIFY(overlay->isVisible());
        QCOMPARE(overlay->findChild<QQuickItem *>()->property("text").toString(), "Drop to add 2 links");
        QDropEvent drop(QPointF(at), Qt::CopyAction | Qt::MoveAction, &links, Qt::LeftButton, Qt::NoModifier);
        QGuiApplication::sendEvent(&view, &drop);
        QTRY_COMPARE(service.lastCommand["op"].toString(), "links.review");
        QCOMPARE(service.lastCommand["text"].toString(), "https://example.invalid/alpha.zip\nhttps://example.invalid/beta.zip");
        QVERIFY(!overlay->isVisible());
        QObject *review = nullptr;
        QTRY_VERIFY((review = view.rootObject()->findChild<QObject *>("browserRequestDialog")) && review->property("visible").toBool());
        QCOMPARE(service.commands, 1);
        view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void malformedChecksumSnapshotsAreRejected_data() {
        QTest::addColumn<QJsonValue>("value");
        const QJsonObject valid{{"id", "9007199254740993"}, {"algorithm", "SHA-256"}, {"state", "done"}, {"percent", 100}, {"digest", QString(64, 'a')}};
        QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("not-object") << QJsonValue("wrong");
        const QList<QPair<QString, QJsonValue>> fields{{"id", 123}, {"algorithm", "SHA-999"}, {"state", "matches"},
            {"percent", -1}, {"percent", 101}, {"percent", 99.5}, {"percent", "100"}, {"percent", 99},
            {"digest", 3}, {"digest", "abc"}, {"digest", QString(64, 'z')}, {"digest", QString(64, 'A')}};
        int i = 0;
        for (const auto &field : fields) {
            auto bad = valid; bad[field.first] = field.second;
            QTest::newRow(qPrintable(QString("bad-%1-%2").arg(field.first).arg(i++))) << QJsonValue(bad);
        }
        auto running = valid; running["state"] = "running";
        QTest::newRow("running-with-digest") << QJsonValue(running);
    }
    void malformedChecksumSnapshotsAreRejected() {
        QFETCH(QJsonValue, value);
        FakeService service; service.detailsCapability = service.checksumCapability = true;
        auto row = service.rows.first().toObject(); row["details"] = QJsonObject{{"url", "https://example.invalid/x"}, {"folder", "/downloads"}, {"error", QJsonValue::Null}, {"fileChecksum", QJsonValue::Null}};
        service.rows = QJsonArray{row};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}, {"checksum", QJsonValue::Null}};
        ServiceDownloads model(service.descriptor); QTRY_READY(model);
        QCOMPARE(model.rowCount(), 1);
        service.extensionSnapshot["checksum"] = value;
        QTRY_VERIFY(model.connectionState() != "ready"); QCOMPARE(model.rowCount(), 1); QVERIFY(model.checksum().isEmpty());
    }
    void checksumDialogUsesCapturedIdsAndExplicitCommands() {
        FakeService service; service.detailsCapability = service.checksumCapability = true;
        const QString id = "9007199254740993", hash(64, 'a');
        service.rows = QJsonArray{QJsonObject{{"id", id}, {"name", "fixture.bin"}, {"size", "1000"}, {"downloaded", "1000"},
            {"state", "completed"}, {"dateAdded", "1234"}, {"details", QJsonObject{{"url", "https://example.invalid/x"}, {"folder", "/downloads"}, {"error", QJsonValue::Null}, {"fileChecksum", "SHA-256:" + hash}}}}};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}, {"checksum", QJsonValue::Null}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model)); QTRY_READY(model);
        QVERIFY(QTest::qWaitForWindowExposed(view.get())); QVERIFY(waitForRenderedFrame(view.get()));
        model.select(id); QCOMPARE(model.selectedId(), id);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openChecksum"));
        auto *dialog = view->rootObject()->findChild<QObject *>("downloadChecksumDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool()); QCOMPARE(service.commands, 0); // Opening never hashes/saves.
        QVERIFY(view->rootObject()->property("modalOpen").toBool());
        auto *dialogContent = dialog->property("contentItem").value<QQuickItem *>(); QVERIFY(dialogContent);
        QVERIFY(waitForRenderedFrame(dialogContent->window()));
        model.select(""); // Later selection changes must not retarget the captured dialog.
        auto *expected = dialog->findChild<QObject *>("checksumExpected"); QVERIFY(expected);
        QCOMPARE(expected->property("text").toString(), hash);
        auto *start = dialog->findChild<QObject *>("checksumStart"); QVERIFY(start);
        QVERIFY(QMetaObject::invokeMethod(start, "clicked")); QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["op"].toString(), "download.checksum"); QCOMPARE(service.lastCommand["action"].toString(), "start"); QCOMPARE(service.lastCommand["id"].toString(), id);
        service.extensionSnapshot["checksum"] = QJsonObject{{"id", id}, {"algorithm", "SHA-256"}, {"state", "running"}, {"percent", 37}, {"digest", ""}};
        auto *cancel = dialog->findChild<QObject *>("checksumCancel"); QVERIFY(cancel); QTRY_VERIFY(cancel->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked")); QTRY_COMPARE(service.commands, 2); QCOMPARE(service.lastCommand["action"].toString(), "cancel");
        service.extensionSnapshot["checksum"] = QJsonObject{{"id", id}, {"algorithm", "SHA-256"}, {"state", "done"}, {"percent", 100}, {"digest", hash}};
        auto *status = dialog->findChild<QObject *>("checksumComparison"); QVERIFY(status);
        QTRY_COMPARE(status->property("text").toString(), "Matches expected checksum");
        expected->setProperty("text", QString(64, 'b'));
        QTRY_COMPARE(status->property("text").toString(), "Does not match expected checksum");
        QTest::qWait(500); QCOMPARE(expected->property("text").toString(), QString(64, 'b'));
        auto *save = dialog->findChild<QObject *>("checksumSave"); QVERIFY(save);
        expected->setProperty("text", "bad"); QVERIFY(!save->property("enabled").toBool());
        expected->setProperty("text", QString(64, 'b')); QTRY_VERIFY(save->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(save, "clicked")); QTRY_COMPARE(service.commands, 3);
        QCOMPARE(service.lastCommand["expected"].toString(), QString(64, 'b')); QCOMPARE(service.lastCommand["id"].toString(), id);
        expected->setProperty("text", ""); QTRY_VERIFY(!model.busy()); QVERIFY(QMetaObject::invokeMethod(save, "clicked"));
        QTRY_COMPARE(service.commands, 4); QVERIFY(service.lastCommand["expected"].isNull());
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            expected->setProperty("text", hash);
            auto *content = qvariant_cast<QQuickItem *>(dialog->property("contentItem")); QVERIFY(content && content->window());
            QVERIFY(waitForRenderedFrame(content->window())); QVERIFY(content->window()->grabWindow().save(capture + "/checksum-matches.png"));
        }
        service.checksumCapability = false; service.restart("older-checksum");
        QTRY_VERIFY(!model.capabilities().contains("download.checksum")); QTRY_READY(model);
        auto *action = view->rootObject()->findChild<QObject *>("checksumDownload"); QVERIFY(action); QVERIFY(!action->property("visible").toBool());
        QVERIFY(!model.checksumCommand(id, "start", "SHA-256", "")); QCOMPARE(service.commands, 4);
        QVERIFY(waitForRenderedFrame(dialogContent->window()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(!view->rootObject()->property("modalOpen").toBool());
        view->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        view->setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void categoryDestinationsAreCapabilityGatedAndDraftsSurvivePolling() {
        FakeService service; service.categoryCapability = service.categoryDestinationCapability = service.runtimeCapability = true;
        service.categorySnapshot = QJsonArray{QJsonObject{{"id", "7"}, {"name", "Custom"}, {"icon", "icon:folder"}, {"path", "/downloads/custom"}, {"usePath", true},
            {"acceptedFileTypes", QJsonArray{"zip"}}, {"acceptedUrlPatterns", QJsonArray{}}, {"items", QJsonArray{}}}};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model)); QTRY_READY(model);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openCategories"));
        auto *dialog = view->rootObject()->findChild<QObject *>("serviceCategoriesDialog"); QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "choose", Q_ARG(QString, "7")));
        auto *toggle = dialog->findChild<QObject *>("categoryUsePath"); QVERIFY(toggle);
        auto *path = dialog->findChild<QObject *>("categoryPath"); QVERIFY(path);
        auto *pathBrowse = dialog->findChild<QObject *>("categoryPathBrowse"); QVERIFY(pathBrowse);
        QTRY_VERIFY(toggle->property("visible").toBool()); QVERIFY(toggle->property("checked").toBool());
        QCOMPARE(path->property("text").toString(), "/downloads/custom");
        QVERIFY(pathBrowse->property("enabled").toBool()); // Follows the "Download Location" toggle.
        path->setProperty("text", "/downloads/edited");
        QTest::qWait(600); QCOMPARE(path->property("text").toString(), "/downloads/edited");
        QVERIFY(QMetaObject::invokeMethod(dialog->findChild<QObject *>("applyServiceCategory"), "clicked"));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["path"].toString(), "/downloads/edited"); QVERIFY(service.lastCommand["usePath"].toBool());
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            auto *content = qvariant_cast<QQuickItem *>(dialog->property("contentItem")); QVERIFY(content && content->window());
            QVERIFY(waitForRenderedFrame(content->window())); QVERIFY(content->window()->grabWindow().save(capture + "/category-destination.png"));
        }
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        // Old services must never receive these extension fields, even if they expose
        // the historical read-only path in category snapshots.
        service.categoryDestinationCapability = false; service.restart("older");
        QTRY_READY(model); QTRY_VERIFY(!model.capabilities().contains("category.destination"));
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openCategories"));
        QVERIFY(!toggle->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "choose", Q_ARG(QString, "7")));
        QVERIFY(QMetaObject::invokeMethod(dialog->findChild<QObject *>("applyServiceCategory"), "clicked"));
        QTRY_COMPARE(service.commands, 2); QVERIFY(!service.lastCommand.contains("path")); QVERIFY(!service.lastCommand.contains("usePath"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    }
    void categoryIconsFollowTheOriginalUrisInSidebarAndEditor() {
        FakeService service; service.categoryCapability = service.categoryIconCapability = true;
        const auto category = [](const QString &id, const QString &name, const QString &icon) {
            return QJsonObject{{"id", id}, {"name", name}, {"icon", icon}, {"path", "/fixture/Downloads"}, {"usePath", false},
                {"acceptedFileTypes", QJsonArray{"zip"}}, {"acceptedUrlPatterns", QJsonArray{}}, {"items", QJsonArray{}}};
        };
        // The original stores historical URI names: "icon:pictureFile" draws its archive icon.
        service.categorySnapshot = {category("0", "Compressed", "icon:pictureFile"), category("3", "Music", "icon:folderOpen"),
            category("4", "Pictures", "icon:fileOpen"), category("9", "Stored", "resource:custom.svg")};
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.categories().size(), 4);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1000, 700);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.show(); QCOMPARE(view.status(), QQuickView::Ready);
        auto *root = view.rootObject();
        QHash<QString, QString> icons;
        for (const auto &entry : root->property("navigationCategories").toList()) icons.insert(entry.toMap()["label"].toString(), entry.toMap()["icon"].toString());
        QCOMPARE(icons["Compressed"], "package-x-generic");
        QCOMPARE(icons["Music"], "audio-x-generic");
        QCOMPARE(icons["Pictures"], "image-x-generic");
        QCOMPARE(icons["Stored"], "text-x-generic");
        QVERIFY(QMetaObject::invokeMethod(root, "openCategories"));
        auto *dialog = root->findChild<QObject *>("serviceCategoriesDialog"); QVERIFY(dialog);
        auto *picker = root->findChild<QObject *>("categoryIconButton"); QVERIFY(picker);
        auto *apply = root->findChild<QObject *>("applyServiceCategory"); QVERIFY(apply);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QTRY_VERIFY(picker->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "choose", Q_ARG(QString, "0")));
        QCOMPARE(picker->property("text").toString(), "Compressed");
        QVERIFY(QMetaObject::invokeMethod(picker, "clicked"));
        auto *popup = root->findChild<QObject *>("categoryIconPopup"); QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        auto *grid = qvariant_cast<QQuickItem *>(popup->property("contentItem"));
        auto *network = visualChild(grid, "categoryIconChoice-36"); QVERIFY(network);
        QCOMPARE(network->property("text").toString(), "Network");
        QVERIFY(visualChild(grid, "categoryIconChoice-2")->property("checked").toBool()); // Current icon.
        QVERIFY(QMetaObject::invokeMethod(network, "clicked"));
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(picker->property("text").toString(), "Network");
        QTRY_VERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "category.update");
        QCOMPARE(service.lastCommand["categoryId"].toString(), "0");
        QCOMPARE(service.lastCommand["icon"].toString(), "icon:network");
        QTRY_VERIFY(!model.busy());
        // An icon the picker does not offer is left as stored: no icon is sent.
        QVERIFY(QMetaObject::invokeMethod(dialog, "choose", Q_ARG(QString, "9")));
        QCOMPARE(dialog->property("iconIndex").toInt(), -1);
        QCOMPARE(picker->property("text").toString(), "Custom");
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QTRY_COMPARE(service.lastCommand["categoryId"].toString(), "9");
        QVERIFY(!service.lastCommand.contains("icon"));
        QTRY_VERIFY(!model.busy());
        // New categories start with the folder icon.
        QVERIFY(QMetaObject::invokeMethod(dialog, "choose", Q_ARG(QString, "")));
        QCOMPARE(picker->property("text").toString(), "Folder");
        view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void tasksMenuDispatchesGlobalCommandsWithoutSelection() {
        FakeService service; service.tasksCapability = true;
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 1);
        QVERIFY(model.selectedId().isEmpty());
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1000, 700);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(view.rootObject()->property("tasksMenuAvailable").toBool());
        QVERIFY(!view.rootObject()->property("toolsMenuAvailable").toBool());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMenu", Q_ARG(QString, "Tasks")));
        auto *menu = view.rootObject()->findChild<QObject *>("tasksMenu"); QVERIFY(menu); QTRY_VERIFY(menu->property("visible").toBool());
        const int hold = qBound(0, qEnvironmentVariableIntValue("QT_TASKS_GALLERY_HOLD_MS"), 10000); if (hold) QTest::qWait(hold);
        for (const auto &[object, operation] : {qMakePair("startAllQueues", "queue.startAll"), qMakePair("stopAllQueues", "queue.stopAll"), qMakePair("stopAllDownloads", "download.stopAll")}) {
            QTRY_VERIFY(!model.busy()); auto *item = view.rootObject()->findChild<QObject *>(object); QVERIFY(item);
            QVERIFY(QMetaObject::invokeMethod(item, "triggered")); QTRY_COMPARE(service.lastCommand["op"].toString(), operation);
            QVERIFY(service.lastCommand["id"].toString().isEmpty()); QVERIFY(service.lastCommand["queueId"].toString().isEmpty());
        }
        QMetaObject::invokeMethod(menu, "close"); QTest::qWait(200); view.close(); QTest::qWait(100);
        view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(100);
    }
    // Offscreen visual review of the parity UI: every visible window (main view, menus and
    // dialogs opened as popup windows) is saved as PNG. Skipped unless a directory is given.
    void parityGallery() {
        const QString dir = qEnvironmentVariable("QT_PARITY_CAPTURE_DIR");
        if (dir.isEmpty()) QSKIP("Set QT_PARITY_CAPTURE_DIR to render the parity gallery");
        QVERIFY(QDir().mkpath(dir));
        FakeService service;
        service.sortCapability = service.monitorCapability = service.tasksCapability = service.actionsCapability = service.detailsCapability = true;
        service.reviewCapability = service.browserCapability = service.runtimeCapability = service.desktopCapability = true;
        service.categoryCapability = service.categoryIconCapability = service.soundCapability = service.powerCapability = service.policyCapability = true;
        {
            // The original defaults with their stored (historical) icon URIs.
            const QList<QPair<QString, QString>> defaults{{"Compressed", "icon:pictureFile"}, {"Programs", "icon:zipFile"}, {"Videos", "icon:musicFile"},
                {"Music", "icon:folderOpen"}, {"Pictures", "icon:fileOpen"}, {"Documents", "icon:videoFile"}};
            for (int i = 0; i < defaults.size(); ++i) service.categorySnapshot.append(QJsonObject{{"id", QString::number(i)}, {"name", defaults[i].first},
                {"icon", defaults[i].second}, {"path", "/downloads"}, {"usePath", false}, {"acceptedFileTypes", QJsonArray{"x"}},
                {"acceptedUrlPatterns", QJsonArray{}}, {"items", QJsonArray{}}});
        }
        auto settings = displaySettings(); settings["qtSortColumn"] = "name"; settings["qtSortDescending"] = false;
        service.extensionSnapshot = {{"queues", QJsonArray{powerQueue()}}, {"powerAction", QJsonValue::Null}, {"manualPending", QJsonArray{"9007199254740995"}}, {"settings", settings}, {"browserRequests", QJsonArray{}},
            {"integration", QJsonObject{{"autoStartOnBoot", false}, {"useSystemTray", false}, {"notificationSound", true}, {"notifications", true},
                {"successNotificationSound", "/home/user/Sounds/finished.wav"}, {"errorNotificationSound", ""}, {"generalNotificationSound", ""}}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"error", QJsonValue::Null}, {"enabled", false}, {"port", 15151}, {"auth", false}}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}}};
        service.extensionSnapshot["settingsPolicy"] = QJsonObject{{"retained", QJsonArray{}}, {"activityAdapter", false}, {"speedLimitActive", false}};
        const auto details = [](const QString &name) { return QJsonObject{{"url", "https://example.invalid/" + name}, {"folder", "/downloads"},
            {"downloadPage", "https://example.invalid/page"}, {"error", QJsonValue::Null}}; };
        service.rows = QJsonArray{
            QJsonObject{{"id", "9007199254740993"}, {"name", "ubuntu-26.04-desktop-amd64.iso"}, {"size", "6200000000"}, {"downloaded", "2100000000"}, {"state", "downloading"}, {"speed", "8400000"}, {"remainingTime", "487"}, {"dateAdded", "100"}, {"details", details("ubuntu.iso")}},
            QJsonObject{{"id", "9007199254740994"}, {"name", "lecture-notes.pdf"}, {"size", "3400000"}, {"downloaded", "1200000"}, {"state", "paused"}, {"speed", "0"}, {"remainingTime", "-1"}, {"dateAdded", "200"}, {"details", details("lecture-notes.pdf")}},
            QJsonObject{{"id", "9007199254740995"}, {"name", "dataset.tar.zst"}, {"size", "-1"}, {"downloaded", "0"}, {"state", "queued"}, {"speed", "0"}, {"remainingTime", "-1"}, {"dateAdded", "300"}, {"details", details("dataset.tar.zst")}},
            QJsonObject{{"id", "9007199254740996"}, {"name", "photos-2026.zip"}, {"size", "880000000"}, {"downloaded", "880000000"}, {"state", "completed"}, {"speed", "0"}, {"remainingTime", "-1"}, {"dateAdded", "50"}, {"details", details("photos-2026.zip")}}};
        for (const auto &name : {"alpha.zip", "beta-release-notes.txt", "gamma.iso"}) service.reviewItems.append(QJsonObject{
            {"url", "https://mirror.example.invalid/files/" + QString(name)}, {"name", name}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}});
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 4);
        model.select("9007199254740994");
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView); view.resize(1120, 700);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.setTitle("AB Download Manager"); view.show();
        QCOMPARE(view.status(), QQuickView::Ready); QVERIFY(QTest::qWaitForWindowExposed(&view));
        int shot = 0;
        const auto capture = [&](const QString &name) {
            QTest::qWait(500); int n = 0;
            for (auto *window : QGuiApplication::topLevelWindows()) {
                auto *quick = qobject_cast<QQuickWindow *>(window);
                if (!quick || !quick->isVisible() || quick->width() < 2) continue;
                const QString file = QStringLiteral("%1/%2-%3-%4.png").arg(dir).arg(shot, 2, 10, QLatin1Char('0')).arg(name).arg(n++);
                QVERIFY2(quick->grabWindow().save(file), qPrintable(file));
            }
            ++shot;
        };
        const auto root = view.rootObject();
        QTest::mouseMove(&view, QPoint(900, 500)); // The offscreen cursor starts over the File button.
        capture("main");
        {
            QMimeData links; links.setUrls({QUrl("https://mirror.example.invalid/a.zip"), QUrl("https://mirror.example.invalid/b.zip")});
            QDragEnterEvent enter(QPoint(600, 350), Qt::CopyAction, &links, Qt::LeftButton, Qt::NoModifier);
            QGuiApplication::sendEvent(&view, &enter); QVERIFY(enter.isAccepted());
            capture("drop-links");
            QDragLeaveEvent left; QGuiApplication::sendEvent(&view, &left);
        }
        QVERIFY(QMetaObject::invokeMethod(root, "openActionsMenu")); capture("actions-menu");
        QMetaObject::invokeMethod(root->findChild<QObject *>("exampleActionsMenu"), "close"); QTest::qWait(200);
        QVERIFY(QMetaObject::invokeMethod(root, "openMenu", Q_ARG(QString, "File"))); capture("file-menu");
        QMetaObject::invokeMethod(root->findChild<QObject *>("fileMenu"), "close"); QTest::qWait(200);
        QVERIFY(QMetaObject::invokeMethod(root, "openMenu", Q_ARG(QString, "Tasks"))); capture("tasks-menu");
        QMetaObject::invokeMethod(root->findChild<QObject *>("tasksMenu"), "close"); QTest::qWait(200);
        QVERIFY(QMetaObject::invokeMethod(root, "openCategories"));
        auto *categories = root->findChild<QObject *>("serviceCategoriesDialog"); QTRY_VERIFY(categories->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(categories, "choose", Q_ARG(QString, "3")));
        capture("categories");
        auto *iconPopup = root->findChild<QObject *>("categoryIconPopup");
        QVERIFY(QMetaObject::invokeMethod(iconPopup, "open")); QTRY_VERIFY(iconPopup->property("opened").toBool()); capture("category-icons");
        QMetaObject::invokeMethod(iconPopup, "close"); QTRY_VERIFY(!iconPopup->property("visible").toBool());
        QMetaObject::invokeMethod(categories, "close"); QTRY_VERIFY(!categories->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
        auto *settingsDialog = root->findChild<QObject *>("serviceSettingsDialog"); QTRY_VERIFY(settingsDialog->property("visible").toBool());
        settingsDialog->setProperty("page", 0); capture("settings-appearance");
        auto *unit = root->findChild<QQuickItem *>("settingsSizeUnit"); QVERIFY(unit);
        QTest::mouseClick(unit->window(), Qt::LeftButton, Qt::NoModifier,
            unit->mapToScene(QPointF(unit->width() / 2, unit->height() / 2)).toPoint());
        auto *unitPopup = unit->property("popup").value<QObject *>(); QVERIFY(unitPopup);
        QTRY_VERIFY(unitPopup->property("opened").toBool()); capture("settings-dropdown");
        QTest::keyClick(unit->window(), Qt::Key_Escape); QTRY_VERIFY(!unitPopup->property("visible").toBool());
        settingsDialog->setProperty("page", 2); capture("settings-integration");
        QMetaObject::invokeMethod(settingsDialog, "close"); QTRY_VERIFY(!settingsDialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root, "openQueues", Q_ARG(QString, "0")));
        auto *queues = root->findChild<QObject *>("serviceQueuesDialog"); QVERIFY(queues);
        QTRY_VERIFY(queues->property("visible").toBool()); capture("queue-power-settings");
        QMetaObject::invokeMethod(queues, "close"); QTRY_VERIFY(!queues->property("visible").toBool());
        service.extensionSnapshot["powerAction"] = pendingPower("1");
        auto *power = root->findChild<QObject *>("powerActionDialog"); QVERIFY(power);
        QTRY_VERIFY(power->property("visible").toBool()); capture("queue-power-warning");
        service.extensionSnapshot["powerAction"] = QJsonValue::Null;
        QTRY_VERIFY(!power->property("visible").toBool());
        auto *about = root->findChild<QObject *>("aboutPreviewDialog"); QVERIFY(about);
        QVERIFY(QMetaObject::invokeMethod(about, "open")); QTRY_VERIFY(about->property("opened").toBool());
        for (int page = 0; page < 3; ++page) {
            root->findChild<QObject *>("aboutPages")->setProperty("currentIndex", page);
            capture(QStringLiteral("about-%1").arg(page));
        }
        QMetaObject::invokeMethod(about, "close"); QTRY_VERIFY(!about->property("visible").toBool());
        auto *exitDialog = root->findChild<QObject *>("confirmExitDialog"); QVERIFY(exitDialog);
        QVERIFY(QMetaObject::invokeMethod(root, "requestExit")); QTRY_VERIFY(exitDialog->property("visible").toBool()); capture("exit-confirm");
        QMetaObject::invokeMethod(exitDialog, "close"); QTRY_VERIFY(!exitDialog->property("visible").toBool());
        auto *mutation = root->findChild<QObject *>("downloadMutationDialog"); QVERIFY(mutation);
        QVERIFY(QMetaObject::invokeMethod(root, "openMutation", Q_ARG(QString, "download.move"))); QTRY_VERIFY(mutation->property("visible").toBool()); capture("move");
        QMetaObject::invokeMethod(mutation, "close"); QTRY_VERIFY(!mutation->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root, "openMutation", Q_ARG(QString, "download.reset"))); QTRY_VERIFY(mutation->property("visible").toBool()); capture("reset");
        QMetaObject::invokeMethod(mutation, "close"); QTRY_VERIFY(!mutation->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(root, "openMultipleAdd"));
        root->findChild<QObject *>("multipleAddLinks")->setProperty("text", QString("https://mirror.example.invalid/files/alpha.zip\n").repeated(6000));
        capture("multiple-too-large");
        root->findChild<QObject *>("multipleAddLinks")->setProperty("text", "https://mirror.example.invalid/files/alpha.zip\nhttps://mirror.example.invalid/files/beta-release-notes.txt\nhttps://mirror.example.invalid/files/gamma.iso");
        capture("multiple-entry");
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QObject *>("reviewMultipleLinks"), "clicked"));
        QObject *review = nullptr;
        QTRY_VERIFY((review = root->findChild<QObject *>("browserRequestDialog")) && review->property("visible").toBool());
        capture("multiple-review");
        // Check Sizes in progress: one row checked, one failed, one still running.
        service.probeBatchCapability = true; service.restart("gallery-epoch");
        QTRY_VERIFY(model.capabilities().contains("browser.probeBatch")); QTRY_VERIFY(!model.busy());
        QVERIFY(QMetaObject::invokeMethod(review->findChild<QObject *>("probeBrowserRequestSizes"), "clicked"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "browser.probeBatch");
        service.setProbe(0, {{"state", "done"}, {"size", "73400320"}, {"statusCode", 206}, {"requiresAuth", false}});
        service.setProbe(2, {{"state", "failed"}});
        QTRY_COMPARE(visualChild(qobject_cast<QQuickItem *>(review->findChild<QObject *>("browserRequestItems")), "browserRequestSize-2")->property("text").toString(), "Unavailable");
        capture("multiple-sizes");
        view.close(); QTest::qWait(200);
        view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(100);
        // Preview mode hides most per-download groups; no separators may double up.
        PreviewDownloads preview;
        QQuickView previewView; previewView.setResizeMode(QQuickView::SizeRootObjectToView); previewView.resize(1120, 700);
        previewView.setInitialProperties({{"downloads", QVariant::fromValue(&preview)}});
        previewView.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); previewView.show();
        QCOMPARE(previewView.status(), QQuickView::Ready); QVERIFY(QTest::qWaitForWindowExposed(&previewView));
        QTest::mouseMove(&previewView, QPoint(900, 500));
        QVERIFY(QMetaObject::invokeMethod(previewView.rootObject(), "openActionsMenu")); capture("preview-actions-menu");
        previewView.close(); QTest::qWait(200);
        previewView.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(100);
    }
    void sortingAndAggregateStatusPreserveStableSelection() {
        FakeService service; service.sortCapability = service.monitorCapability = true;
        auto settings = basicSettings(); settings["qtSortColumn"] = "dateAdded"; settings["qtSortDescending"] = true;
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"manualPending", QJsonArray{"9007199254740995"}}, {"settings", settings}};
        service.rows = QJsonArray{
            QJsonObject{{"id", "9007199254740993"}, {"name", "Zeta.bin"}, {"size", "1000"}, {"downloaded", "250"}, {"state", "downloading"}, {"speed", "1200"}, {"remainingTime", "1"}, {"dateAdded", "100"}},
            QJsonObject{{"id", "9007199254740994"}, {"name", "alpha.bin"}, {"size", "9007199254740993"}, {"downloaded", "0"}, {"state", "downloading"}, {"speed", "800"}, {"remainingTime", "999999"}, {"dateAdded", "200"}},
            QJsonObject{{"id", "9007199254740995"}, {"name", "unknown.bin"}, {"size", "-1"}, {"downloaded", "0"}, {"state", "queued"}, {"speed", "0"}, {"remainingTime", "-1"}, {"dateAdded", "300"}}};
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 3);
        QCOMPARE(model.idAt(0), "9007199254740995"); model.selectAll();
        QCOMPARE(model.statistics()["selectedBytes"].toString(), "9007199254741993");
        QCOMPARE(model.statistics()["unknownSizes"].toInt(), 1); QCOMPARE(model.statistics()["queuedCount"].toInt(), 1);
        QCOMPARE(model.statistics()["speedLabel"].toString(), "1.95 KiB/s");
        QVERIFY(model.sortBy("name")); QTRY_COMPARE(model.sortColumn(), "name");
        QCOMPARE(model.idAt(0), "9007199254740994"); QCOMPARE(model.selectionCount(), 3);
        QTRY_VERIFY(!model.busy()); QVERIFY(model.sortBy("size")); QTRY_COMPARE(model.sortColumn(), "size");
        QCOMPARE(model.idAt(0), "9007199254740993"); QCOMPARE(model.idAt(2), "9007199254740995");
        QTRY_VERIFY(!model.busy()); QVERIFY(model.sortBy("size")); QTRY_VERIFY(model.sortDescending());
        QCOMPARE(model.idAt(0), "9007199254740994"); QCOMPARE(model.idAt(2), "9007199254740995"); // Unknown stays last.
        QTRY_VERIFY(!model.busy()); QVERIFY(!model.sortBy("not-a-column"));
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.resize(1120, 700); view.setMinimumSize(QSize(1120, 700)); view.setMaximumSize(QSize(1120, 700));
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.setTitle("Parity list fixture"); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        auto *status = view.rootObject()->findChild<QObject *>("downloadsSelectedSummary"); QVERIFY(status);
        QTRY_VERIFY(status->property("text").toString().contains("3 selected"));
        const int hold = qBound(0, qEnvironmentVariableIntValue("QT_LIST_GALLERY_HOLD_MS"), 10000);
        if (hold) { QVERIFY(reviewFrame(view, qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR", service.directory.path()) + "/list-client.png", QSize(1120, 700))); QTest::qWait(hold); }
        model.setSearch("Zeta"); QCOMPARE(model.selectionCount(), 1); QCOMPARE(model.statistics()["selectedBytes"].toString(), "1000");
        QCOMPARE(model.statistics()["queuedCount"].toInt(), 1); // Global, not filtered.
        model.setSearch("no such download"); QCOMPARE(model.selectionCount(), 0);
        QTRY_COMPARE(status->property("text").toString(), QString()); // No "0 selected · 0 B".
        view.close(); QTest::qWait(100);
        view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(100);
    }
    void capturedDownloadActionsAndLinks() {
        FakeService service; service.detailsCapability = service.actionsCapability = true;
        auto row = service.rows[0].toObject(); const auto id = row["id"].toString();
        row["state"] = "paused";
        row["details"] = QJsonObject{{"url", "http://127.0.0.1:12345/source"}, {"folder", "/tmp"}, {"downloadPage", "https://example.invalid/page#section"}, {"error", QJsonValue::Null}};
        service.rows[0] = row;
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 1); model.select(id);
        QVERIFY(model.copyDownloadLinks()); QCOMPARE(QGuiApplication::clipboard()->text(), "http://127.0.0.1:12345/source");
        QCOMPARE(model.downloadPageUrl(id), QUrl("https://example.invalid/page#section"));
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMutation", Q_ARG(QString, "download.move")));
        auto *dialog = view.rootObject()->findChild<QObject *>("downloadMutationDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("visible").toBool());
        view.rootObject()->findChild<QObject *>("moveDownloadName")->setProperty("text", "renamed.bin");
        model.select(""); // A confirmation acts on the captured ID, never the later selection.
        const int hold = qBound(0, qEnvironmentVariableIntValue("QT_ACTIONS_GALLERY_HOLD_MS"), 10000);
        auto *confirm = view.rootObject()->findChild<QObject *>("confirmDownloadMutation"); QVERIFY(confirm);
        QTRY_VERIFY(confirm->property("highlighted").toBool());
        QTRY_VERIFY(qvariant_cast<QQuickItem *>(confirm->property("background"))->isVisible());
        if (hold) QTest::qWait(hold);
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "download.move"); QCOMPARE(service.lastCommand["id"].toString(), id);
        QCOMPARE(service.lastCommand["name"].toString(), "renamed.bin");
        QTRY_VERIFY(!model.busy()); model.select(id);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMutation", Q_ARG(QString, "download.reset")));
        QTRY_VERIFY(dialog->property("visible").toBool()); if (hold) QTest::qWait(hold);
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked")); QTRY_COMPARE(service.lastCommand["op"].toString(), "download.reset");
        QTRY_VERIFY(!model.busy());
        row["state"] = "downloading"; service.rows[0] = row;
        QTRY_VERIFY(!model.canMutateDownload(id, "download.reset")); QVERIFY(!model.resetDownload(id));
        // Invalid page extension never replaces the coherent snapshot or launches a handler.
        auto details = row["details"].toObject(); details["downloadPage"] = "file:///tmp/not-a-page"; row["details"] = details;
        service.rows[0] = row; QTRY_VERIFY(!model.errorMessage().isEmpty());
        QCOMPARE(model.downloadPageUrl(id), QUrl("https://example.invalid/page#section"));
        QMetaObject::invokeMethod(dialog, "close"); QTest::qWait(300); view.close(); QTest::qWait(100);
        view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(100);
    }
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
        QTRY_READY(model);
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
        QTRY_READY(model);
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
    void settingsDoesNotBlockIndependentDownloadWindows() {
        FakeService service; service.browserCapability = true; service.fulfillBrowserAccept = true;
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()}, {"browserRequests", QJsonArray{}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QTRY_READY(model);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSettings"));
        auto *settings = view->rootObject()->findChild<QObject *>("serviceSettingsDialog"); QVERIFY(settings);
        QTRY_VERIFY(settings->property("opened").toBool());
        auto *settingsContent = settings->property("contentItem").value<QQuickItem *>(); QVERIFY(settingsContent);
        QVERIFY(QTest::qWaitForWindowExposed(settingsContent->window()));
        const QString id = service.rows.first().toObject()["id"].toString();
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "showDownloadWindow", Q_ARG(QString, id), Q_ARG(bool, false)));
        auto *progress = downloadWindow(*view, id); QVERIFY(progress);
        QVERIFY(QTest::qWaitForWindowExposed(progress));
        auto *pause = progress->findChild<QQuickItem *>("downloadWindowPause"); QVERIFY(pause);
        QTRY_VERIFY(pause->isVisible() && pause->isEnabled());
        QVERIFY(waitForRenderedFrame(progress));
        QTest::mouseClick(progress, Qt::LeftButton, Qt::NoModifier,
            pause->mapToScene(QPointF(pause->width() / 2, pause->height() / 2)).toPoint());
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["op"].toString(), "pause");
        QVERIFY(settings->property("opened").toBool());
        service.extensionSnapshot["browserRequests"] = QJsonArray{QJsonObject{{"id", "while-settings"}, {"source", "browser"},
            {"outcomeUnknown", false}, {"items", QJsonArray{QJsonObject{{"url", "https://example.invalid/settings.bin"},
                {"name", "settings.bin"}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}}}}}};
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view->rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(review)); QVERIFY(waitForRenderedFrame(review));
        auto *add = review->findChild<QQuickItem *>("pauseBrowserRequest"); QVERIFY(add);
        QTRY_VERIFY(add->isVisible() && add->isEnabled());
        QTest::mouseClick(review, Qt::LeftButton, Qt::NoModifier,
            add->mapToScene(QPointF(add->width() / 2, add->height() / 2)).toPoint());
        QTRY_COMPARE(service.commands, 2);
        QCOMPARE(service.lastCommand["op"].toString(), "browser.accept");
        QCOMPARE(service.lastCommand["start"].toBool(), false);
        QVERIFY(settings->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        progress->close();
    }
    void dialogsShowTheirTitleOnceAndOneFooterLine() {
        FakeService service; service.policyCapability = true;
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()},
            {"settingsPolicy", QJsonObject{{"retained", QJsonArray{}}, {"activityAdapter", false}, {"speedLimitActive", false}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QTRY_READY(model);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSettings"));
        auto *settings = view->rootObject()->findChild<QObject *>("serviceSettingsDialog"); QVERIFY(settings);
        QTRY_VERIFY(settings->property("opened").toBool());
        // The native title bar shows the title: UiDialog adds no in-window heading or line
        // under it, and the desktop style's own header only appears for in-window
        // Popup.Item dialogs. (Reading `header` would force that deferred style item.)
        QCOMPARE(settings->property("popupType").toInt(), 1); // Popup.Window: natively titled
        QCOMPARE(settings->property("title").toString(), QString("Settings"));
        // The sidebar is divided from its page by a line spanning the content area, as in
        // the main window.
        auto *content = settings->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        auto *divider = content->findChild<QQuickItem *>("paneSeparator"); QVERIFY(divider && divider->isVisible());
        auto *footer = settings->property("footer").value<QQuickItem *>(); QVERIFY(footer);
        QVERIFY(waitForRenderedFrame(content->window()));
        QQuickItem *line = divider->childItems().value(0); QVERIFY(line);
        const auto lineBounds = line->mapRectToScene(QRectF(QPointF(), line->size()));
        QVERIFY2(lineBounds.top() <= 1, qPrintable(QString("line starts at %1").arg(lineBounds.top())));
        QVERIFY(qAbs(lineBounds.bottom() - footer->mapToScene(QPointF()).y()) <= 1);
        QCOMPARE(service.commands, 0);
        QVERIFY(QMetaObject::invokeMethod(settings, "close"));
        QTRY_VERIFY(!settings->property("visible").toBool());
        view->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        view->setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        // Let the fixture's fragmented replies finish before its sockets are destroyed.
        QTest::qWait(300);
    }
    void settingsDropdownSurvivesPointerReleaseAndSelects() {
        FakeService service; service.policyCapability = true;
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", displaySettings()},
            {"settingsPolicy", QJsonObject{{"retained", QJsonArray{}}, {"activityAdapter", false}, {"speedLimitActive", false}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QTRY_READY(model);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSettings"));
        auto *dialog = view->rootObject()->findChild<QObject *>("serviceSettingsDialog"); QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        dialog->setProperty("page", 0);
        auto *combo = view->rootObject()->findChild<QQuickItem *>("settingsSizeUnit"); QVERIFY(combo);
        QTRY_VERIFY(combo->isVisible() && combo->width() > 0);
        auto *window = combo->window(); QVERIFY(window && window != view.get());
        QVERIFY(QTest::qWaitForWindowExposed(window)); QVERIFY(waitForRenderedFrame(window));
        const auto center = combo->mapToScene(QPointF(combo->width() / 2, combo->height() / 2)).toPoint();
        auto *popup = combo->property("popup").value<QObject *>(); QVERIFY(popup);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::qWait(800); // Opening release, focus handoff and multiple service polls.
        QCOMPARE(model.connectionState(), "ready"); QVERIFY(model.errorMessage().isEmpty());
        QVERIFY2(popup->property("opened").toBool(), "dropdown closed after its opening click");
        auto *content = popup->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        auto *list = content; // Native styles may wrap their ListView in another item.
        if (!list->property("currentIndex").isValid()) {
            for (auto *item : content->findChildren<QQuickItem *>())
                if (item->property("currentIndex").isValid()) { list = item; break; }
        }
        QVERIFY(list->property("currentIndex").isValid());
        // Select a real delegate with a pointer event, not currentIndex assignment.
        QQuickItem *choice = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, choice), Q_ARG(int, 1)));
        QVERIFY(choice && choice->window());
        QTest::mouseClick(choice->window(), Qt::LeftButton, Qt::NoModifier,
            choice->mapToScene(QPointF(choice->width() / 2, choice->height() / 2)).toPoint());
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 1);
        // Reopening and dismissing must work too, without applying the draft.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::keyClick(content->window(), Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(service.commands, 0);
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        // Reopen native Settings and the combo repeatedly in the same QML engine.
        for (int repeat = 0; repeat < 4; ++repeat) {
            QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSettings"));
            QTRY_VERIFY(dialog->property("opened").toBool());
            QVERIFY(waitForRenderedFrame(combo->window()));
            QTest::mouseClick(combo->window(), Qt::LeftButton, Qt::NoModifier,
                combo->mapToScene(QPointF(combo->width() / 2, combo->height() / 2)).toPoint());
            QTRY_VERIFY(popup->property("opened").toBool());
            QTest::qWait(150); QVERIFY(popup->property("opened").toBool());
            QTest::keyClick(combo->window(), Qt::Key_Escape);
            QTRY_VERIFY(!popup->property("visible").toBool());
            QVERIFY(dialog->property("opened").toBool());
            QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
            QTRY_VERIFY(!dialog->property("visible").toBool());
        }
        QCOMPARE(service.commands, 0);
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
    void forgetCanAlsoDeleteFiles() {
        FakeService service; service.forgetCapability = true;
        auto row = service.rows[0].toObject(); row["state"] = "paused"; service.rows[0] = row;
        ServiceDownloads model(service.descriptor);
        QTRY_COMPARE(model.rowCount(), 1);
        QVERIFY(model.canDeleteFiles());
        QVERIFY(model.forgetDownloads({row["id"].toString()}, true));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["op"].toString(), "download.forget");
        QCOMPARE(service.lastCommand["keepFiles"], QJsonValue(false));
        QTRY_COMPARE(model.rowCount(), 0);
    }
    void removeDialogDeletesFilesOnlyWhenChecked() {
        FakeService service; service.forgetCapability = true;
        auto row = service.rows[0].toObject(); row["state"] = "paused"; service.rows[0] = row;
        auto second = row; second["id"] = "9007199254740995"; service.rows.append(second);
        PreviewDownloads resources;
        ServiceDownloads model(service.descriptor);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(760, 600); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QTRY_COMPARE(model.rowCount(), 2);
        auto *check = view.rootObject()->findChild<QObject *>("removeDeleteFiles");
        auto *confirm = view.rootObject()->findChild<QObject *>("confirmRemoveExample");
        QVERIFY(check && confirm);
        model.select(model.idAt(0)); QTRY_VERIFY(model.canRemove());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
        QTRY_VERIFY(check->property("visible").toBool()); QVERIFY(!check->property("checked").toBool());
        check->setProperty("checked", true);
        QCOMPARE(confirm->property("text").toString(), QStringLiteral("Delete"));
        if (const int hold = qBound(0, qEnvironmentVariableIntValue("QT_REMOVE_GALLERY_HOLD_MS"), 10000)) QTest::qWait(hold);
        QTRY_VERIFY(confirm->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_COMPARE(service.commands, 1); QCOMPARE(service.lastCommand["keepFiles"], QJsonValue(false));
        // Reopening resets the choice: deletion is never carried over to the next removal.
        QTRY_COMPARE(model.rowCount(), 1); model.select(model.idAt(0)); QTRY_VERIFY(model.canRemove());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
        QVERIFY(!check->property("checked").toBool());
        QCOMPARE(confirm->property("text").toString(), QStringLiteral("Remove"));
        QTRY_VERIFY(confirm->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
        QTRY_COMPARE(service.commands, 2); QCOMPARE(service.lastCommand["keepFiles"], QJsonValue(true));
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
        QTRY_READY(model); QTest::qWait(400); QCOMPARE(service.commands, 3);
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
        QTRY_READY(model); QTest::qWait(500);
        QCOMPARE(windowCount(view), windows); QCOMPARE(requested.count(), hinted); QCOMPARE(service.commands, commands);
        // A same-epoch serial regression is malformed and rejected as a whole.
        service.extensionSnapshot["downloadWindows"] = hints(3, {{ids[4], "started"}});
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(requested.count(), hinted);
        // A service restart (new epoch, serials restart) is a fresh baseline: nothing reopens.
        model.clearError();
        service.extensionSnapshot["downloadWindows"] = hints(2, {{ids[0], "started"}, {ids[1], "failed"}});
        service.restart("restarted-epoch");
        QTRY_READY(model); QTest::qWait(500);
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
    static QJsonObject displaySettings() {
        auto settings = basicSettings();
        settings["sizeUnit"] = "BinaryBytes"; settings["speedUnit"] = "BinaryBytes"; settings["useCategoryByDefault"] = true;
        settings["useSpeedLimit"] = "Disabled"; settings["speedLimit"] = "262144"; settings["useAverageSpeed"] = true;
        return settings;
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
        QTRY_READY(model);
        QTest::qWait(700);
        QCOMPARE(service.commandLog.size(), 1); // The queued step is never replayed automatically.
        QVERIFY(model.errorMessage().contains("unknown"));
        // An explicit retry is allowed and is the only thing that clears the warning.
        service.loseCommandReply = false;
        QVERIFY(model.pauseSelected());
        QTRY_COMPARE(service.commandLog.size(), 3);
        QTRY_VERIFY(model.errorMessage().isEmpty());
        QTRY_COMPARE(stateOf(model, ids[0]), "paused");
        // ids[0] was already paused before the retry. The successful reply clears
        // the error before its final snapshot arrives; wait for the changed row,
        // not a cached row that makes the preceding QTRY succeed immediately.
        QTRY_COMPARE(stateOf(model, ids[2]), "paused");
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
        QTRY_READY(model);
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
    void longDuplicateNameKeepsExistingActionInline() {
        FakeService service; service.detailsCapability = service.runtimeCapability = service.browserCapability = true;
        const QString name = QString(170, 'W') + ".mp4", link = "https://example.invalid/long.mp4";
        service.rows = QJsonArray{QJsonObject{{"id", "9007199254740993"}, {"name", name}, {"size", "1000"},
            {"downloaded", "0"}, {"state", "paused"}, {"dateAdded", "1234"},
            {"details", QJsonObject{{"url", link}, {"folder", "/downloads"}, {"error", QJsonValue::Null}}}}};
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}},
            {"browserRequests", QJsonArray{QJsonObject{{"id", "long-duplicate"}, {"source", "browser"}, {"outcomeUnknown", false},
                {"items", QJsonArray{QJsonObject{{"url", link}, {"name", name}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}}}}}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QTRY_READY(model);
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view->rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        auto *notice = review->findChild<QQuickItem *>("browserRequestExisting"); QVERIFY(notice);
        QTRY_VERIFY(notice->isVisible()); QVERIFY(waitForRenderedFrame(review));
        QTest::qWait(350); // Let the warning's first-show animation/style finish before capturing.
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) QVERIFY(review->grabWindow().save(capture + "/long-duplicate.png"));
        // A long name must not create a second action row or grow the banner: the title,
        // elided detail and actions share one row; only the policy hint sits below it.
        auto *message = notice->findChild<QQuickItem *>("duplicateNoticeText"); QVERIFY(message);
        auto *hint = notice->findChild<QQuickItem *>("browserRequestDuplicateHint"); QVERIFY(hint && hint->isVisible());
        auto *show = notice->findChild<QQuickItem *>("duplicateNoticeShowExisting"); QVERIFY(show && show->isVisible());
        auto *resume = notice->findChild<QQuickItem *>("duplicateNoticeResume"); QVERIFY(resume && resume->isVisible());
        // Replace needs the replace capability; without it there is no choice to make.
        QVERIFY(!notice->findChild<QQuickItem *>("browserRequestDuplicateChoice")->isVisible());
        const auto checkRow = [&] {
            const auto buttonBounds = show->mapRectToItem(notice, QRectF(QPointF(), show->size()));
            const auto textBounds = message->mapRectToItem(notice, QRectF(QPointF(), message->size()));
            const auto hintBounds = hint->mapRectToItem(notice, QRectF(QPointF(), hint->size()));
            QVERIFY(buttonBounds.left() >= 0 && buttonBounds.right() <= notice->width());
            QVERIFY(textBounds.right() <= resume->mapRectToItem(notice, QRectF(QPointF(), resume->size())).left());
            QVERIFY2(buttonBounds.bottom() <= hintBounds.top(), "actions must stay on the title row");
            QVERIFY(qAbs(resume->mapRectToItem(notice, QRectF(QPointF(), resume->size())).center().y() - buttonBounds.center().y()) < 1);
        };
        checkRow();
        const qreal wideHeight = notice->height();
        QVERIFY2(wideHeight <= 112, qPrintable(QString("duplicate banner height %1").arg(wideHeight)));
        review->setMinimumWidth(360); review->setMaximumWidth(1000); review->resize(420, review->height());
        QVERIFY(waitForRenderedFrame(review));
        checkRow();
        QVERIFY2(notice->height() <= wideHeight + 1, "narrow review must retain a single action row");
        QVERIFY(notice->width() <= review->width());
        QCOMPARE(message->property("text").toString(), QString("%1 · Paused").arg(name));
        QVERIFY(message->property("truncated").toBool());
        if (!capture.isEmpty()) QVERIFY(review->grabWindow().save(capture + "/long-duplicate-narrow.png"));
        QCOMPARE(service.commands, 0);
        review->close();
    }
    void duplicatesAreChosenInlineBeforeAdding() {
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
        QCOMPARE(view.status(), QQuickView::Ready); QTRY_READY(model);
        QTRY_COMPARE(model.rowCount(), 1);
        QTRY_COMPARE(model.settings()["qtDuplicateAction"].toString(), "ask");
        const auto object = [&](const char *name) { return view.rootObject()->findChild<QObject *>(name); };
        const auto openAdd = [&] {
            QMetaObject::invokeMethod(view.rootObject(), "openAddDialog");
            object("exampleUrlInput")->setProperty("text", link);
            object("exampleNameInput")->setProperty("text", "archive.zip");
        };
        // Optional compositor gallery: holds each state on screen for decorated captures.
        const int galleryHold = qBound(0, qEnvironmentVariableIntValue("QT_DUPLICATE_GALLERY_HOLD_MS"), 10000);
        auto *addDialog = object("addExampleDialog"); QVERIFY(addDialog);
        const auto bannerCurrent = [&] {
            QString key; QMetaObject::invokeMethod(addDialog, "inputKey", Q_RETURN_ARG(QString, key));
            return addDialog->property("checkedKey").toString() == key;
        };
        // A click before the debounced check has shown the banner only shows it: nothing
        // is added before the duplicate and its choice were visible.
        openAdd();
        QVERIFY(!bannerCurrent());
        QVERIFY(QMetaObject::invokeMethod(object("confirmAddExample"), "clicked"));
        QVERIFY(bannerCurrent()); QVERIFY(object("addExistingWarning")->property("visible").toBool());
        QVERIFY(view.rootObject()->property("addDialogVisible").toBool());
        QTest::qWait(400); QCOMPARE(service.commands, 0);
        QVERIFY(!object("duplicateDownloadDialog")); // No second dialog any more.
        QVERIFY(object("addDuplicateChoice")->property("visible").toBool());
        QVERIFY(!object("addDuplicateHint")->property("visible").toBool());
        QVERIFY(object("addKeepBoth")->property("checked").toBool());
        if (galleryHold) QTest::qWait(galleryHold);
        if (const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR"); !capture.isEmpty()) {
            auto *warning = qobject_cast<QQuickItem *>(object("addExistingWarning")); QVERIFY(warning && warning->window());
            QVERIFY(waitForRenderedFrame(warning->window())); QVERIFY(warning->window()->grabWindow().save(capture + "/add-duplicate-banner.png"));
        }
        // Keep both (the default) adds a numbered copy on the next click.
        QVERIFY(QMetaObject::invokeMethod(object("confirmAddExample"), "clicked"));
        QTRY_COMPARE(service.commands, 1); QCOMPARE(service.lastCommand["duplicatePolicy"].toString(), "numbered");
        QTRY_VERIFY(!model.busy()); QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
        // Replace, chosen with a real click, is sent as overwrite without another dialog.
        openAdd(); QTRY_VERIFY(bannerCurrent());
        QVERIFY(object("addKeepBoth")->property("checked").toBool());
        auto *addReplace = qobject_cast<QQuickItem *>(object("addReplace")); QVERIFY(addReplace && addReplace->window());
        QTRY_VERIFY(addReplace->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(addReplace->window())); QVERIFY(waitForRenderedFrame(addReplace->window()));
        QTest::mouseClick(addReplace->window(), Qt::LeftButton, Qt::NoModifier,
            addReplace->mapToScene(QPointF(addReplace->height() / 2, addReplace->height() / 2)).toPoint());
        QTRY_VERIFY(addReplace->property("checked").toBool());
        QCOMPARE(addDialog->property("inlinePolicy").toString(), "overwrite");
        QVERIFY(QMetaObject::invokeMethod(object("confirmStartDownload"), "clicked"));
        QTRY_COMPARE(service.commands, 2); QCOMPARE(service.lastCommand["duplicatePolicy"].toString(), "overwrite");
        QTRY_VERIFY(!model.busy()); QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
        // Reopening starts from Keep both again.
        openAdd(); QVERIFY(object("addKeepBoth")->property("checked").toBool());
        QCOMPARE(addDialog->property("inlinePolicy").toString(), "numbered");
        QVERIFY(QMetaObject::invokeMethod(addDialog, "close"));
        QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
        // Existing means show only: no automatic resume, execution or new Add.
        settings["qtDuplicateAction"] = "existing"; service.extensionSnapshot["settings"] = settings;
        QTRY_COMPARE(model.settings()["qtDuplicateAction"].toString(), "existing");
        openAdd(); QVERIFY(QMetaObject::invokeMethod(object("confirmAddExample"), "clicked"));
        QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
        QCOMPARE(service.commands, 2); QVERIFY(downloadWindow(view, id));
        if (galleryHold) { view.hide(); QTest::qWait(galleryHold); downloadWindow(view, id)->close(); view.show(); }
        // Browser intake uses the same safe numbered policy only after a review choice.
        settings["qtDuplicateAction"] = "ask"; service.extensionSnapshot["settings"] = settings;
        service.extensionSnapshot["browserRequests"] = QJsonArray{QJsonObject{{"id", "duplicate-browser"}, {"outcomeUnknown", false},
            {"items", QJsonArray{QJsonObject{{"url", link}, {"name", "archive.zip"}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}}}}}};
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view.rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        // The review already says the link exists, before Download is pressed.
        QTRY_VERIFY(review->findChild<QObject *>("browserRequestExisting")->property("visible").toBool());
        if (galleryHold) { view.hide(); QTest::qWait(galleryHold); }
        // A single link chooses inline: the banner offers Keep both/Replace and Download
        // applies the choice without a second confirmation dialog.
        auto *choice = review->findChild<QQuickItem *>("browserRequestDuplicateChoice"); QVERIFY(choice);
        QTRY_VERIFY(choice->isVisible());
        QVERIFY(!review->findChild<QQuickItem *>("browserRequestDuplicateHint")->isVisible());
        auto *replace = review->findChild<QQuickItem *>("browserRequestReplace"); QVERIFY(replace);
        QVERIFY(review->findChild<QObject *>("browserRequestKeepBoth")->property("checked").toBool());
        QVERIFY(QTest::qWaitForWindowExposed(review)); QVERIFY(waitForRenderedFrame(review));
        QTest::mouseClick(review, Qt::LeftButton, Qt::NoModifier,
            replace->mapToScene(QPointF(replace->height() / 2, replace->height() / 2)).toPoint());
        QTRY_VERIFY(replace->property("checked").toBool());
        QCOMPARE(review->property("inlinePolicy").toString(), "overwrite");
        if (galleryHold) QTest::qWait(galleryHold);
        if (const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR"); !capture.isEmpty()) {
            QVERIFY(waitForRenderedFrame(review)); QVERIFY(review->grabWindow().save(capture + "/review-duplicate-choice.png"));
        }
        service.commandResult = QJsonObject{{"ok", true}, {"id", id}, {"ids", QJsonArray{id}}};
        auto *accept = review->findChild<QObject *>("acceptBrowserRequest"); QVERIFY(accept);
        // The accepted review closes and destroys itself, its duplicate dialog with it.
        const QPointer<QObject> browserDuplicate = review->findChild<QObject *>("browserDuplicateDialog"); QVERIFY(browserDuplicate);
        QVERIFY(QMetaObject::invokeMethod(accept, "clicked"));
        QTRY_COMPARE(service.commands, 3);
        QVERIFY(!browserDuplicate || !browserDuplicate->property("visible").toBool());
        QCOMPARE(service.lastCommand["op"].toString(), "browser.accept");
        QCOMPARE(service.lastCommand["duplicatePolicy"].toString(), "overwrite");
        QCOMPARE(service.lastCommand["requestId"].toString(), "duplicate-browser");
    }
    void browserReviewCategoryDropdownNeverCoversTheField() {
        FakeService service; service.categoryCapability = service.runtimeCapability = service.browserCapability = true;
        QJsonArray categories;
        for (const auto &name : {"Compressed", "Programs", "Videos", "Music", "Pictures", "Documents"})
            categories.append(QJsonObject{{"id", QString::number(categories.size() + 1)}, {"name", name}, {"icon", "icon:folder"}, {"path", "/downloads"}, {"usePath", false},
                {"acceptedFileTypes", QJsonArray{}}, {"acceptedUrlPatterns", QJsonArray{}}, {"items", QJsonArray{}}});
        service.categorySnapshot = categories;
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}},
            {"browserRequests", QJsonArray{QJsonObject{{"id", "category-review"}, {"source", "browser"}, {"outcomeUnknown", false},
                {"items", QJsonArray{QJsonObject{{"url", "https://example.invalid/clip.mp4"}, {"name", "clip.mp4"}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}}}}}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QTRY_READY(model);
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view->rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        auto *combo = review->findChild<QQuickItem *>("browserRequestCategory"); QVERIFY(combo);
        QTRY_VERIFY(combo->isVisible() && combo->width() > 0);
        QTRY_COMPARE(combo->property("count").toInt(), 8);
        QVERIFY(QTest::qWaitForWindowExposed(review)); QVERIFY(waitForRenderedFrame(review));
        auto *popup = combo->property("popup").value<QObject *>(); QVERIFY(popup);
        const auto center = combo->mapToScene(QPointF(combo->width() / 2, combo->height() / 2)).toPoint();
        // The content-sized review is too short for the list below the field.
        const qreal comboBottom = combo->mapToScene(QPointF(0, combo->height())).y();
        const auto open = [&] {
            QTest::mouseClick(review, Qt::LeftButton, Qt::NoModifier, center);
            QTRY_VERIFY(popup->property("opened").toBool());
            QTest::qWait(500); // Opening release and service polls.
            QVERIFY2(popup->property("opened").toBool(), "dropdown closed after its opening click");
        };
        const auto list = [&]() -> QQuickItem * {
            auto *content = popup->property("contentItem").value<QQuickItem *>();
            if (content && content->property("currentIndex").isValid()) return content;
            for (auto *item : content ? content->findChildren<QQuickItem *>() : QList<QQuickItem *>{})
                if (item->property("currentIndex").isValid()) return item;
            return nullptr;
        };
        open();
        QVERIFY2(comboBottom + popup->property("implicitHeight").toReal() > review->height(), "fixture must be a short review");
        // A short window never covers the field: the list opens on the side with more
        // room, inside the window, and scrolls when it is clipped.
        const auto checkPlacement = [&](bool expectBelow) {
            const qreal y = popup->property("y").toReal(), height = popup->property("height").toReal();
            QVERIFY2(y + height <= 0 || y >= combo->height(), qPrintable(QString("popup %1+%2 overlaps the field").arg(y).arg(height)));
            QCOMPARE(y >= combo->height(), expectBelow);
            QVERIFY(combo->mapToScene(QPointF(0, y)).y() >= 0 && combo->mapToScene(QPointF(0, y + height)).y() <= review->height());
            QCOMPARE(list()->property("interactive").toBool(), height < popup->property("implicitHeight").toReal());
        };
        checkPlacement(false);
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) QVERIFY(review->grabWindow().save(capture + "/review-category-short.png"));
        QQuickItem *choice = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list(), "itemAtIndex", Q_RETURN_ARG(QQuickItem *, choice), Q_ARG(int, 4)));
        QVERIFY(choice && choice->window() == review);
        // Wheel-scroll the clipped list until the row is inside the visible part.
        for (int step = 0; step < 10 && choice->mapToItem(list(), QPointF(0, choice->height())).y() > list()->height(); ++step) {
            QWheelEvent wheel(list()->mapToScene(QPointF(list()->width() / 2, list()->height() / 2)), review->mapToGlobal(list()->mapToScene(QPointF(list()->width() / 2, list()->height() / 2))),
                QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QGuiApplication::sendEvent(review, &wheel);
            QVERIFY(waitForRenderedFrame(review));
        }
        QTRY_VERIFY(choice->mapToItem(list(), QPointF(0, choice->height())).y() <= list()->height() + 1);
        QTest::mouseClick(review, Qt::LeftButton, Qt::NoModifier,
            choice->mapToScene(QPointF(choice->width() / 2, choice->height() / 2)).toPoint());
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 4);
        // With room below (a taller window), it opens below the field at full height.
        review->setMaximumHeight(2000); review->resize(review->width(), int(comboBottom + popup->property("implicitHeight").toReal() + 40));
        QVERIFY(waitForRenderedFrame(review));
        open();
        checkPlacement(true);
        QCOMPARE(popup->property("height").toReal(), popup->property("implicitHeight").toReal());
        if (!capture.isEmpty()) QVERIFY(review->grabWindow().save(capture + "/review-category-tall.png"));
        QTest::keyClick(review, Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 4);
        QCOMPARE(service.commands, 0);
        review->close();
    }
    void browserMultiLinkDuplicatesAskOnceWithoutRedundantActions() {
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
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}},
            {"browserRequests", QJsonArray{QJsonObject{{"id", "duplicate-batch"}, {"outcomeUnknown", false},
                {"items", QJsonArray{QJsonObject{{"url", link}, {"name", "archive.zip"}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}},
                                     QJsonObject{{"url", "https://example.invalid/new.zip"}, {"name", "new.zip"}, {"hasHeaders", false}, {"downloadPage", QJsonValue::Null}}}}}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QTRY_READY(model);
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view->rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        // Rows flag duplicates; the single-link banner is not used for batches.
        QVERIFY(!review->findChild<QObject *>("browserRequestExisting")->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(review->findChild<QObject *>("acceptBrowserRequest"), "clicked"));
        auto *duplicates = review->findChild<QObject *>("browserDuplicateDialog"); QVERIFY(duplicates);
        QTRY_VERIFY(duplicates->property("visible").toBool()); QCOMPARE(service.commands, 0);
        // Batch replacement is unsupported, so Replace is hidden rather than disabled.
        QVERIFY(!review->findChild<QObject *>("browserDuplicateOverwrite")->property("visible").toBool());
        auto *keep = review->findChild<QQuickItem *>("browserDuplicateAddNew"); QVERIFY(keep);
        QVERIFY(keep->isVisible());
        // Every footer button fits inside the dialog.
        auto *content = qvariant_cast<QQuickItem *>(duplicates->property("contentItem")); QVERIFY(content);
        auto *footer = qvariant_cast<QQuickItem *>(duplicates->property("footer")); QVERIFY(footer);
        for (auto *button : footer->findChildren<QQuickItem *>()) {
            if (!button->isVisible() || !button->inherits("QQuickAbstractButton")) continue;
            const auto bounds = button->mapRectToItem(footer, QRectF(QPointF(), button->size()));
            QVERIFY2(bounds.left() >= 0 && bounds.right() <= footer->width(), qPrintable(button->property("text").toString()));
        }
        QVERIFY(QMetaObject::invokeMethod(keep, "clicked"));
        QTRY_COMPARE(service.commands, 1);
        QCOMPARE(service.lastCommand["op"].toString(), "browser.accept");
        QCOMPARE(service.lastCommand["duplicatePolicy"].toString(), "numbered");
        QCOMPARE(service.lastCommand["requestId"].toString(), "duplicate-batch");
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
    void partSlotModelKeepsExactRangesAndIncrementalIdentity() {
        DownloadPartSlots slotModel;
        QAbstractItemModelTester tester(&slotModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy resets(&slotModel, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(&slotModel, &QAbstractItemModel::rowsInserted);
        QSignalSpy removes(&slotModel, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changes(&slotModel, &QAbstractItemModel::dataChanged);
        const auto range = [](qint64 offset, const QString &state = "downloading", int done = 25, int length = 100) {
            return QVariantMap{{"id", QString::number(offset)}, {"offset", QString::number(offset)}, {"state", state},
                {"length", QString::number(length)}, {"downloaded", QString::number(done)}};
        };
        const auto update = [&](const QVariantList &items, int budget = 8) {
            slotModel.update({{"available", true}, {"items", items}}, budget);
        };
        const qint64 base = 9007199254740993LL;
        const auto at = [&](int row) { return slotModel.data(slotModel.index(row), DownloadPartSlots::PartData).toMap(); };
        QVariantList parts; for (int i = 0; i < 12; ++i) parts.append(range(base + i));
        update(parts); QCOMPARE(slotModel.rowCount(), 12); // Never hide real active ranges at the configured budget.
        inserts.clear(); changes.clear();
        parts[1] = range(base + 1, "completed", 100);
        parts[2] = range(base + 2, "idle", 0);
        parts[0] = range(base, "downloading", 50, 80); // Dynamic split changes the old range's endpoint.
        parts.append(range(base + 12, "connecting", 0)); parts.append(range(base + 13));
        update(parts);
        QCOMPARE(slotModel.rowCount(), 12);
        QCOMPARE(at(0)["length"].toString(), "80"); QCOMPARE(at(0)["downloaded"].toString(), "50");
        QCOMPARE(at(1)["id"].toString(), QString::number(base + 12));
        QCOMPARE(at(2)["id"].toString(), QString::number(base + 13));
        QCOMPARE(at(3)["id"].toString(), QString::number(base + 3));
        QVERIFY(!changes.isEmpty()); QCOMPARE(resets.count(), 0); QCOMPARE(inserts.count(), 0); QCOMPARE(removes.count(), 0);
        changes.clear(); update(parts); QCOMPARE(changes.count(), 0);
        // Missing ranges vacate slots, never leave stale offsets/progress presented as live.
        parts.removeAt(3); update(parts); QCOMPARE(at(3)["id"].toString(), QString());
        QCOMPARE(at(3)["state"].toString(), "idle");
        slotModel.update({}, 8); QCOMPARE(slotModel.rowCount(), 12); // Cold/disconnected cache is not retirement.
        slotModel.update({{"available", false}, {"items", QVariantList{}}}, 8);
        QCOMPARE(slotModel.rowCount(), 0); QCOMPARE(removes.count(), 1); QCOMPARE(resets.count(), 0);
        // Reopening late does not create a row per historical completed engine range.
        parts.clear(); for (int i = 0; i < 256; ++i) parts.append(range(base + i, i >= 254 ? "downloading" : "completed", i >= 254 ? 25 : 100));
        update(parts); QCOMPARE(slotModel.rowCount(), 8);
        QSet<QString> ids; for (int i = 0; i < slotModel.rowCount(); ++i) ids.insert(at(i)["id"].toString());
        QVERIFY(ids.contains(QString::number(base + 254))); QVERIFY(ids.contains(QString::number(base + 255)));
    }
    static QJsonObject partRange(int number, const QString &state = "downloading", int downloaded = 25) {
        const QString offset = QString::number(number * 100);
        return {{"id", offset}, {"offset", offset}, {"length", "100"}, {"downloaded", QString::number(downloaded)}, {"state", state}};
    }
    static void setParts(FakeService &service, const QString &id, const QJsonArray &items) {
        service.partSnapshots[id] = QJsonObject{{"available", true}, {"totalCount", items.size()}, {"truncated", false}, {"resumeSupport", true}, {"items", items}};
    }
    void progressReusesFinishedPartSlotsWithoutChangingRanges() {
        FakeService service; service.partsCapability = true;
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}};
        const QString id = service.rows.first().toObject()["id"].toString();
        QJsonArray parts; for (int i = 0; i < 8; ++i) parts.append(partRange(i));
        setParts(service, id, parts);
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model)); QTRY_READY(model);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "showDownloadWindow", Q_ARG(QString, id), Q_ARG(bool, false)));
        auto *window = downloadWindow(*view, id); QVERIFY(window); QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *list = window->findChild<QQuickItem *>("downloadWindowPartList"); QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 8);
        QQuickItem *slot = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, slot), Q_ARG(int, 1)));
        QVERIFY(slot); QPointer<QQuickItem> sameSlot(slot);
        // A finished engine range stays in the raw projection; new split work reuses
        // displayed slot 2, not a ninth cumulative-history row.
        parts[1] = partRange(1, "completed", 100); parts.append(partRange(8, "connecting", 0));
        setParts(service, id, parts);
        QTRY_COMPARE(model.downloadParts(id)["items"].toList().size(), 9);
        QTest::qWait(350);
        QCOMPARE(list->property("count").toInt(), 8);
        QVERIFY(sameSlot); QCOMPARE(sameSlot->property("index").toInt(), 1);
        QTRY_COMPARE(sameSlot->property("modelData").toMap()["id"].toString(), partRange(8)["id"].toString());
        QCOMPARE(sameSlot->property("modelData").toMap()["offset"].toString(), partRange(8)["offset"].toString());
        QCOMPARE(sameSlot->property("modelData").toMap()["downloaded"].toString(), "0");
        QCOMPARE(sameSlot->property("modelData").toMap()["length"].toString(), "100");
        // A snapshot reorder cannot renumber a still-running range or rebuild slots.
        QJsonArray reversed; for (auto i = parts.size(); i > 0; --i) reversed.append(parts[i - 1]);
        setParts(service, id, reversed); QTest::qWait(600);
        QVERIFY(sameSlot); QCOMPARE(sameSlot->property("modelData").toMap()["id"].toString(), partRange(8)["id"].toString());
        QCOMPARE(model.downloadParts(id)["items"].toList().size(), 9); // Raw piece-bar data is untouched.
        window->close();
    }
    void progressPartsKeepScrollPositionDuringUpdatesAndTurnover() {
        FakeService service; service.partsCapability = true;
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}}};
        const QString id = service.rows.first().toObject()["id"].toString();
        auto download = service.rows.first().toObject(); download["size"] = "2500"; service.rows[0] = download;
        QJsonArray parts; for (int i = 0; i < 24; ++i) parts.append(partRange(i));
        setParts(service, id, parts);
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model)); QTRY_READY(model);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "showDownloadWindow", Q_ARG(QString, id), Q_ARG(bool, false)));
        auto *window = downloadWindow(*view, id); QVERIFY(window); QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *list = window->findChild<QQuickItem *>("downloadWindowPartList"); QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 24); QVERIFY(waitForRenderedFrame(window));
        const QPointF center = list->mapToScene(QPointF(list->width() / 2, list->height() / 2));
        QWheelEvent wheel(center, window->mapToGlobal(center.toPoint()), QPoint(), QPoint(0, -720), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(list->property("contentY").toReal() > 0); QTest::qWait(500);
        const qreal position = list->property("contentY").toReal(); QVERIFY(position > 0);
        for (int i = 0; i < 6; ++i) {
            parts[12] = partRange(12, "downloading", 26 + i); setParts(service, id, parts);
            QTRY_COMPARE(model.downloadParts(id)["items"].toList()[12].toMap()["downloaded"].toString(), QString::number(26 + i));
            QTest::qWait(100);
            QCOMPARE(list->property("contentY").toReal(), position);
        }
        parts[12] = partRange(12, "completed", 100); parts.append(partRange(24)); setParts(service, id, parts);
        QTRY_COMPARE(model.downloadParts(id)["items"].toList().size(), 25); QTest::qWait(400);
        QCOMPARE(list->property("contentY").toReal(), position);
        QCOMPARE(list->property("count").toInt(), 24);
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/parts-scrolled-turnover.png"));
        window->close();
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
        QCOMPARE(view.status(), QQuickView::Ready); QTRY_READY(model);
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
        // Actions use the shared bottom footer (separator, right-aligned) like the
        // completion page and every dialog, below the parts rather than between sections.
        auto *close = window->findChild<QQuickItem *>("downloadWindowClose"); QVERIFY(close && close->isVisible());
        const auto closeBounds = close->mapRectToScene(QRectF(QPointF(), close->size()));
        QVERIFY(closeBounds.top() >= list->mapRectToScene(QRectF(QPointF(), list->size())).bottom());
        QVERIFY2(window->width() - closeBounds.right() < 40, "footer actions are right-aligned");
        QVERIFY2(window->height() - closeBounds.bottom() < 40, "footer actions sit at the window bottom");
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
        auto *content = window->findChild<QObject *>("downloadContent"); QVERIFY(content);
        QTRY_COMPARE(window->height(), qCeil(content->property("contentHeight").toReal()));
        QVERIFY(waitForRenderedFrame(window));
        if (!capture.isEmpty() || hold) QTest::qWait(350); // Preserve opt-in gallery settling.
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
        QCOMPARE(view->status(), QQuickView::Ready); QTRY_READY(model);
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
        QCOMPARE(view->status(), QQuickView::Ready); QTRY_READY(model);
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
    void singleBrowserReviewGeometrySurvivesDelayedPolling() {
        FakeService service; service.browserCapability = service.categoryCapability = service.runtimeCapability = true;
        service.extensionSnapshot = {{"settings", basicSettings()}, {"queues", QJsonArray{}},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/downloads"}, {"destinationRoots", QJsonArray{"/downloads"}}, {"redirects", true}, {"proxy", "direct"}}},
            {"browserIntegration", QJsonObject{{"state", "disabled"}, {"enabled", false}, {"auth", false}, {"port", 15151}, {"error", QJsonValue::Null}}},
            {"browserRequests", QJsonArray{QJsonObject{{"id", "single-delayed"}, {"source", "browser"}, {"outcomeUnknown", false},
                {"items", QJsonArray{QJsonObject{{"url", "https://example.invalid/single.bin"}, {"name", "single.bin"}, {"hasHeaders", true}, {"downloadPage", QJsonValue::Null}}}}}}}};
        PreviewDownloads resources; ServiceDownloads model(service.descriptor);
        std::unique_ptr<QQuickView> view(liveView(model));
        QQuickWindow *review = nullptr;
        QTRY_VERIFY((review = view->rootObject()->findChild<QQuickWindow *>("browserRequestDialog")) && review->isVisible());
        const QSize exposed = firstExposedSize(review);
        const auto capture = qEnvironmentVariable("QT_ADAPTER_CAPTURE_DIR");
        QSignalSpy heights(review, &QWindow::heightChanged);
        QSignalSpy widths(review, &QWindow::widthChanged);
        QVERIFY(waitForRenderedFrame(review));
        if (!capture.isEmpty()) {
            QTest::qWait(350); // Opt-in image settling, still observed by both geometry spies.
            QVERIFY(review->grabWindow().save(capture + "/single-review-first.png"));
        }
        for (int i = 0; i < 12; ++i) {
            auto row = service.rows.first().toObject(); row["downloaded"] = QString::number(250 + i); service.rows[0] = row;
            QTest::qWait(350); // Real delayed polling/style/layout after the configure guard expires.
            QCOMPARE(review->size(), exposed);
        }
        for (const auto &change : heights) QCOMPARE(change.first().toInt(), exposed.height());
        for (const auto &change : widths) QCOMPARE(change.first().toInt(), exposed.width());
        QVERIFY(service.snapshots >= 12);
        if (!capture.isEmpty()) QVERIFY(review->grabWindow().save(capture + "/single-review-delayed.png"));
        review->close();
    }
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
        QCOMPARE(view->status(), QQuickView::Ready); QTRY_READY(model);
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
            if (qEnvironmentVariableIsSet("QT_CAPTURE_OPEN_COMBO")) {
                auto *combo = review->findChild<QObject *>("browserRequestCategory"); QVERIFY(combo);
                QTimer::singleShot(1500, combo, [combo] { combo->property("popup").value<QObject *>()->setProperty("visible", true); });
            }
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
        QCOMPARE(view.status(), QQuickView::Ready); QTRY_READY(model);
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
        service.restart("review-new-epoch"); QTRY_READY(model);
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
        QTRY_VERIFY(model.errorMessage().contains("unknown")); QTRY_READY(model);
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
        QTRY_READY(model);
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
        // Do not destroy the engine in the middle of native popup/deferred style
        // construction. This capture-only case previously returned immediately
        // after opening a second native dialog, intermittently corrupting teardown.
        auto *http = view.rootObject()->findChild<QObject *>("httpOptionsDialog"); QVERIFY(http);
        QTRY_VERIFY(http->property("opened").toBool());
        auto *content = http->property("contentItem").value<QQuickItem *>(); QVERIFY(content);
        QVERIFY(waitForRenderedFrame(content->window()));
        QVERIFY(QMetaObject::invokeMethod(http, "close")); QTRY_VERIFY(!http->property("visible").toBool());
        auto *add = view.rootObject()->findChild<QObject *>("addExampleDialog"); QVERIFY(add);
        QVERIFY(QMetaObject::invokeMethod(add, "close")); QTRY_VERIFY(!add->property("visible").toBool());
        view.close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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
    static QJsonObject powerQueue() {
        return {{"id", "0"}, {"name", "Main"}, {"active", false}, {"maxConcurrent", 2}, {"stopOnEmpty", true},
            {"items", QJsonArray{}}, {"powerAction", "Shutdown"}, {"schedule", QJsonObject{{"enabledStartTime", false}, {"enabledEndTime", false},
                {"startTime", "02:30"}, {"endTime", "07:30"}, {"daysOfWeek", QJsonArray{"MONDAY"}}}}};
    }
    static QJsonObject pendingPower(const QString &serial) {
        return {{"serial", serial}, {"type", "Shutdown"}, {"reason", "queueFinished"}, {"queueId", "0"}, {"queueName", "Main"}};
    }
    void queuePowerActionsAreConfiguredAndAnsweredOnce() {
        FakeService service; service.runtimeCapability = service.powerCapability = true;
        service.extensionSnapshot = {{"queues", QJsonArray{powerQueue()}}, {"settings", basicSettings()}, {"powerAction", QJsonValue::Null},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/fixture/downloads"},
                {"destinationRoots", QJsonArray{"/fixture/downloads"}}, {"redirects", true}, {"proxy", "direct"}}}};
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.connectionState(), "ready"); QCOMPARE(model.queues().size(), 1);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready); QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *root = view.rootObject();
        QVERIFY(QMetaObject::invokeMethod(root, "openQueues", Q_ARG(QString, "0")));
        auto *queues = root->findChild<QObject *>("serviceQueuesDialog");
        auto *combo = root->findChild<QObject *>("queuePowerAction");
        auto *apply = root->findChild<QObject *>("applyQueueSettings");
        QVERIFY(queues && combo && apply); QTRY_VERIFY(combo->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 1);
        for (int index : {2, 3, 1, 0}) {
            QTRY_VERIFY(!model.busy()); combo->setProperty("currentIndex", index);
            const int before = service.commands;
            QVERIFY(QMetaObject::invokeMethod(apply, "click"));
            QTRY_COMPARE(service.commands, before + 1);
            QCOMPARE(service.lastCommand["op"].toString(), "queue.update");
            QCOMPARE(service.lastCommand["queueId"].toString(), "0");
            QVERIFY(service.lastCommand.contains("powerAction"));
            const QJsonValue expected = index == 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(QStringList{"", "Shutdown", "Sleep", "Hibernate"}[index]);
            QCOMPARE(service.lastCommand["powerAction"], expected);
        }
        QTRY_VERIFY(!model.busy());
        QVERIFY(QMetaObject::invokeMethod(queues, "close")); QTRY_VERIFY(!queues->property("visible").toBool());
        auto *dialog = root->findChild<QObject *>("powerActionDialog");
        auto *cancel = root->findChild<QObject *>("powerActionCancel");
        auto *now = root->findChild<QObject *>("powerActionNow");
        QVERIFY(dialog && cancel && now);
        // A cleared service snapshot closes an unanswered dialog without any command.
        const int before = service.commands;
        service.extensionSnapshot["powerAction"] = pendingPower("1");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(root->property("mainModalOpen").toBool());
        QCOMPARE(dialog->property("remaining").toInt(), 30);
        QVERIFY(!now->property("highlighted").toBool());
        service.extensionSnapshot["powerAction"] = QJsonValue::Null;
        QTRY_VERIFY(!dialog->property("visible").toBool()); QCOMPARE(service.commands, before);
        // Cancel answers the serial; an unchanged snapshot must not reopen the dialog.
        service.extensionSnapshot["powerAction"] = pendingPower("2");
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
        QTRY_COMPARE(service.commands, before + 1);
        QCOMPARE(service.lastCommand["op"].toString(), "power.cancel");
        QCOMPARE(service.lastCommand["serial"].toString(), "2");
        QTRY_VERIFY(!model.busy()); QTest::qWait(350);
        QVERIFY(!dialog->property("visible").toBool());
        QVERIFY(!model.answerPowerAction("999", true));
        // Now and the countdown both send power.execute exactly once.
        for (const auto &serial : {QString("3"), QString("4")}) {
            service.extensionSnapshot["powerAction"] = pendingPower(serial);
            QTRY_VERIFY(dialog->property("opened").toBool());
            const int commands = service.commands;
            if (serial == "3") {
                QVERIFY(QMetaObject::invokeMethod(now, "click"));
                QVERIFY(QMetaObject::invokeMethod(now, "clicked")); // Even a second activation is ignored.
            } else dialog->setProperty("remaining", 1); // Exercise the real timer without a 30 s sleep.
            QTRY_COMPARE(service.commands, commands + 1);
            QCOMPARE(service.lastCommand["op"].toString(), "power.execute");
            QCOMPARE(service.lastCommand["serial"].toString(), serial);
            QTRY_VERIFY(!model.busy()); QTest::qWait(1100);
            QCOMPARE(service.commands, commands + 1); QVERIFY(!dialog->property("visible").toBool());
        }
        // A busy command slot postpones the answer; Cancel overrides a queued execution.
        service.extensionSnapshot["powerAction"] = pendingPower("5");
        QTRY_VERIFY(dialog->property("opened").toBool());
        service.holdSnapshots = true; const int snapshots = service.snapshots;
        QTRY_VERIFY(service.snapshots > snapshots);
        QVERIFY(model.queueCommand("queue.update", "0", {{"name", "Main"}, {"maxConcurrent", 2}, {"stopOnEmpty", true}}));
        QVERIFY(model.busy());
        const int commands = service.commands;
        QVERIFY(QMetaObject::invokeMethod(now, "click"));
        QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
        QCOMPARE(service.commands, commands);
        service.holdSnapshots = false; service.releaseHeld();
        QTRY_COMPARE(service.commands, commands + 2);
        QCOMPARE(service.lastCommand["op"].toString(), "power.cancel");
        QCOMPARE(service.lastCommand["serial"].toString(), "5");
        QTRY_VERIFY(!model.busy()); QTRY_VERIFY(!dialog->property("visible").toBool());
        service.extensionSnapshot["powerAction"] = QJsonValue::Null; QTRY_VERIFY(model.powerAction().isEmpty());
        view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void queuePowerActionsRespectRuntimeAndCapabilities_data() {
        QTest::addColumn<bool>("explicitRuntime"); QTest::addColumn<bool>("capability");
        QTest::newRow("supported") << true << true;
        QTest::newRow("isolated") << false << true;
        QTest::newRow("older-service") << true << false;
    }
    void queuePowerActionsRespectRuntimeAndCapabilities() {
        QFETCH(bool, explicitRuntime); QFETCH(bool, capability);
        FakeService service; service.runtimeCapability = true; service.powerCapability = capability;
        service.extensionSnapshot = {{"queues", QJsonArray{powerQueue()}}, {"settings", basicSettings()}, {"powerAction", pendingPower("1")},
            {"runtime", QJsonObject{{"mode", explicitRuntime ? "explicit-runtime" : "isolated"}, {"destination", "/fixture/downloads"},
                {"destinationRoots", QJsonArray{"/fixture/downloads"}}, {"redirects", false}, {"proxy", "direct"}}}};
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 1);
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready); QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *root = view.rootObject(); auto *dialog = root->findChild<QObject *>("powerActionDialog"); QVERIFY(dialog);
        const bool supported = explicitRuntime && capability;
        // Includes a request already present when the view is constructed.
        QTRY_COMPARE(dialog->property("visible").toBool(), supported);
        if (supported) { service.extensionSnapshot["powerAction"] = QJsonValue::Null; QTRY_VERIFY(!dialog->property("visible").toBool()); }
        QVERIFY(QMetaObject::invokeMethod(root, "openQueues", Q_ARG(QString, "0")));
        auto *queues = root->findChild<QObject *>("serviceQueuesDialog"); QVERIFY(queues);
        QTRY_VERIFY(queues->property("opened").toBool());
        auto *combo = root->findChild<QObject *>("queuePowerAction"); QVERIFY(combo);
        QCOMPARE(combo->property("visible").toBool(), supported);
        QCOMPARE(service.commands, 0);
        view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void malformedPowerActionsAreRejected_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
        QTest::newRow("queue-type") << QString("queue") << QJsonValue("Restart");
        QTest::newRow("queue-number") << QString("queue") << QJsonValue(1);
        QTest::newRow("queue-object") << QString("queue") << QJsonValue(QJsonObject{});
        QTest::newRow("queue-missing") << QString("queue") << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("pending-array") << QString("pending") << QJsonValue(QJsonArray{});
        QTest::newRow("pending-missing") << QString("pending") << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("pending-type") << QString("type") << QJsonValue("Restart");
        QTest::newRow("pending-reason") << QString("reason") << QJsonValue("downloadFinished");
        QTest::newRow("serial-number") << QString("serial") << QJsonValue(1);
        QTest::newRow("serial-negative") << QString("serial") << QJsonValue("-1");
        QTest::newRow("serial-zero") << QString("serial") << QJsonValue("0");
        QTest::newRow("serial-overflow") << QString("serial") << QJsonValue("9223372036854775808");
        QTest::newRow("serial-noncanonical") << QString("serial") << QJsonValue("01");
        QTest::newRow("queue-id") << QString("queueId") << QJsonValue(false);
        QTest::newRow("empty-name") << QString("queueName") << QJsonValue("");
        QTest::newRow("long-name") << QString("queueName") << QJsonValue(QString(121, 'x'));
    }
    void malformedPowerActionsAreRejected() {
        QFETCH(QString, field); QFETCH(QJsonValue, value);
        FakeService service; service.powerCapability = true;
        auto queue = powerQueue(); auto pending = pendingPower("1");
        if (field == "queue") queue["powerAction"] = value;
        else if (field != "pending") pending[field] = value;
        service.extensionSnapshot = {{"queues", QJsonArray{queue}}, {"settings", basicSettings()}, {"powerAction", field == "pending" ? value : QJsonValue(pending)}};
        ServiceDownloads model(service.descriptor);
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        QCOMPARE(model.connectionState(), "disconnected"); QVERIFY(model.powerAction().isEmpty());
        QCOMPARE(service.commands, 0);
    }
    void notificationSoundFilesAreEditedOnTheIntegrationPage() {
        FakeService service; service.desktopCapability = service.runtimeCapability = service.soundCapability = true;
        service.extensionSnapshot = {{"queues", QJsonArray{}}, {"settings", basicSettings()},
            {"runtime", QJsonObject{{"mode", "explicit-runtime"}, {"destination", "/fixture/downloads"},
                {"destinationRoots", QJsonArray{"/fixture/downloads"}}, {"redirects", true}, {"proxy", "direct"}}},
            {"integration", QJsonObject{{"autoStartOnBoot", false}, {"useSystemTray", true}, {"notificationSound", true}, {"notifications", true},
                {"successNotificationSound", "/home/user/done.wav"}, {"errorNotificationSound", ""}, {"generalNotificationSound", ""}}},
            {"frontend", QJsonObject{{"activation", QJsonValue::Null}}}};
        ServiceDownloads model(service.descriptor); QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.integration().value("successNotificationSound").toString(), "/home/user/done.wav");
        QQuickView view; view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml")); view.resize(1000, 700); view.show();
        QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openSettings"));
        auto *dialog = view.rootObject()->findChild<QObject *>("serviceSettingsDialog"); QVERIFY(dialog);
        dialog->setProperty("page", 2);
        auto *success = view.rootObject()->findChild<QObject *>("settingsSuccessSound");
        auto *error = view.rootObject()->findChild<QObject *>("settingsErrorSound");
        auto *general = view.rootObject()->findChild<QObject *>("settingsGeneralSound");
        auto *apply = view.rootObject()->findChild<QObject *>("applyDesktopIntegration");
        QVERIFY(success && error && general && apply);
        QTRY_VERIFY(success->property("visible").toBool());
        QCOMPARE(success->property("text").toString(), "/home/user/done.wav");
        QCOMPARE(error->property("placeholderText").toString(), "Default");
        error->setProperty("text", "  /home/user/oops.wav ");
        QVERIFY(QMetaObject::invokeMethod(apply, "click"));
        QTRY_COMPARE(service.lastCommand["op"].toString(), "settings.integration");
        const auto patch = service.lastCommand["settings"].toObject();
        QCOMPARE(patch["successNotificationSound"].toString(), "/home/user/done.wav");
        QCOMPARE(patch["errorNotificationSound"].toString(), "/home/user/oops.wav");
        QCOMPARE(patch["generalNotificationSound"].toString(), "");
        QTRY_VERIFY(!model.busy());
        // The fields follow the Notification Sound switch.
        view.rootObject()->findChild<QObject *>("settingsNotificationSound")->setProperty("checked", false);
        QTRY_VERIFY(!success->property("enabled").toBool());
        // A non-text sound value is rejected like any other malformed field.
        auto integration = service.extensionSnapshot["integration"].toObject(); integration["errorNotificationSound"] = 5;
        service.extensionSnapshot["integration"] = integration;
        QTRY_VERIFY(model.errorMessage().contains("Invalid service response"));
        view.close(); view.setSource(QUrl()); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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
        QTRY_READY(model);
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
        QTRY_READY(model);
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
        // The fixture URL already belongs to earlier smoke downloads: the inline banner
        // shows it first (default Keep both), then Add proceeds with a numbered copy.
        auto *existingWarning = view.rootObject()->findChild<QObject *>("addExistingWarning");
        QVERIFY(existingWarning);
        if (!QMetaObject::invokeMethod(add, "clicked") || view.rootObject()->property("addDialogVisible").toBool()) {
            QTRY_VERIFY(existingWarning->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(add, "clicked"));
        }
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
        if (view.rootObject()->property("addDialogVisible").toBool()) {
            QTRY_VERIFY(existingWarning->property("visible").toBool());
            QVERIFY(QMetaObject::invokeMethod(start, "clicked"));
        }
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

// Repeated Qt/KDE desktop-style QML-engine teardown intermittently corrupts the
// heap on this stack. This is not acceptance of arbitrary engine reuse. Exercise
// every case in its own process (like the real one-engine frontend),
// without retries, skipped cases or hiding a child failure. Explicit case/gallery
// invocation retains the usual QtTest command-line contract.
int main(int argc, char **argv) {
    if (argc > 1) {
        QApplication app(argc, argv);
        ServiceDownloadsTest test;
        return QTest::qExec(&test, argc, argv);
    }
    QCoreApplication app(argc, argv);
    const auto &meta = ServiceDownloadsTest::staticMetaObject;
    bool jobsOk = false;
    const auto jobsValue = qEnvironmentVariable("ABDM_TEST_JOBS");
    int jobs = jobsValue.isEmpty() ? qMin(8, qMax(1, QThread::idealThreadCount())) : jobsValue.toInt(&jobsOk);
    if (!jobsValue.isEmpty() && (!jobsOk || jobs < 1)) {
        qCritical() << "ABDM_TEST_JOBS must be a positive integer";
        return 2;
    }
    // Galleries share capture paths/display interaction; real-engine opt-ins share
    // one service. Preserve their sequential contract even with an explicit job count.
    for (const auto &entry : QProcessEnvironment::systemEnvironment().toStringList()) {
        const auto key = entry.section('=', 0, 0);
        if (key == "QT_SERVICE_TEST_DESCRIPTOR" || (key.startsWith("QT_")
            && (key.endsWith("_HOLD_MS") || key.endsWith("_CAPTURE_DIR")))) jobs = 1;
    }
    QStringList cases, failures;
    for (int i = meta.methodOffset(); i < meta.methodCount(); ++i) {
        const auto method = meta.method(i); const auto name = method.name();
        // QtTest invokes each _data provider with its test; it is not a standalone case.
        if (method.methodType() != QMetaMethod::Slot || method.parameterCount() || method.returnMetaType().id() != QMetaType::Void || name.endsWith("_data")
            || name == "initTestCase" || name == "cleanupTestCase" || name == "init" || name == "cleanup") continue;
        cases.append(QString::fromLatin1(name));
    }
    struct Case {
        QString name;
        QProcess process;
        QElapsedTimer elapsed;
        QByteArray output;
    };
    std::vector<std::unique_ptr<Case>> running;
    int next = 0;
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) {
        qCritical() << "Cannot open test output";
        return 2;
    }
    const auto finish = [&](Case &test, const QString &error = {}) {
        test.output += test.process.readAll();
        output.write(test.output);
        // Write directly: desktop Qt logging rules can suppress qInfo outside QtTest.
        output.write(QStringLiteral("CASE %1: %2 ms (%3)\n").arg(test.name).arg(test.elapsed.elapsed())
            .arg(error.isEmpty() ? QStringLiteral("passed") : error).toUtf8());
        output.flush();
        if (!error.isEmpty()) failures.append(test.name);
    };
    output.write(QStringLiteral("Running %1 isolated cases with %2 jobs\n").arg(cases.size()).arg(jobs).toUtf8());
    output.flush();
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        for (auto it = running.begin(); it != running.end();) {
            auto &test = **it;
            test.output += test.process.readAll();
            if (test.process.state() == QProcess::NotRunning) {
                finish(test, test.process.exitStatus() != QProcess::NormalExit || test.process.exitCode() != 0
                    ? QStringLiteral("failed, exit %1").arg(test.process.exitCode()) : QString());
                it = running.erase(it);
            } else if (test.elapsed.elapsed() >= 120000) {
                test.process.kill();
                test.process.waitForFinished(5000);
                finish(test, QStringLiteral("timed out after 120 s"));
                it = running.erase(it);
            } else ++it;
        }
        while (next < cases.size() && int(running.size()) < jobs) {
            auto test = std::make_unique<Case>();
            test->name = cases[next++];
            test->process.setProcessChannelMode(QProcess::MergedChannels);
            test->elapsed.start();
            test->process.start(QCoreApplication::applicationFilePath(), {test->name, QStringLiteral("-nocrashhandler")});
            if (!test->process.waitForStarted(5000)) {
                finish(*test, QStringLiteral("cannot start: %1").arg(test->process.errorString()));
            } else running.push_back(std::move(test));
        }
        if (next == cases.size() && running.empty()) loop.quit();
    });
    poll.start(10);
    loop.exec();
    if (!failures.isEmpty()) {
        output.write(("Failing cases: " + failures.join(", ") + '\n').toUtf8());
        output.flush();
    }
    return failures.isEmpty() ? 0 : 1;
}
#include "tst_servicedownloads.moc"
