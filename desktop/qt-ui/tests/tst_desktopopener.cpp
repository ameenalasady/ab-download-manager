#include "desktopopener.h"
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

// Registered only on dbus-run-session's disposable bus. No real portal, MIME handler
// or file manager is contacted and no downloaded executable is ever launched.
class PortalFixture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.OpenURI")
public:
    int calls = 0;
    QString path;
    QVariantMap options;
public slots:
    QDBusObjectPath OpenFile(const QString &, const QDBusUnixFileDescriptor &fd, const QVariantMap &value) {
        ++calls;
        path = QFileInfo(QStringLiteral("/proc/self/fd/%1").arg(fd.fileDescriptor())).symLinkTarget();
        options = value;
        return QDBusObjectPath(QStringLiteral("/org/freedesktop/portal/desktop/request/fixture/1"));
    }
    QDBusObjectPath OpenURI(const QString &, const QString &value, const QVariantMap &) {
        ++uris; uri = value;
        return QDBusObjectPath(QStringLiteral("/org/freedesktop/portal/desktop/request/fixture/2"));
    }
public:
    int uris = 0;
    QString uri;
};
class FileManagerFixture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
    int items = 0, folders = 0;
    QStringList urls;
public slots:
    void ShowItems(const QStringList &value, const QString &) { ++items; urls = value; }
    void ShowFolders(const QStringList &value, const QString &) { ++folders; urls = value; }
};
class DesktopOpenerTest : public QObject {
    Q_OBJECT
private slots:
    void usesSessionServicesWithARealFileDescriptor() {
        QVERIFY2(!qEnvironmentVariableIsEmpty("ABDM_OPENER_PRIVATE_BUS"), "Run via the CTest dbus-run-session wrapper only");
        auto bus = QDBusConnection::sessionBus(); QVERIFY(bus.isConnected());
        PortalFixture portal; FileManagerFixture manager;
        QVERIFY(bus.registerService("org.freedesktop.portal.Desktop"));
        QVERIFY(bus.registerObject("/org/freedesktop/portal/desktop", &portal, QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService("org.freedesktop.FileManager1"));
        QVERIFY(bus.registerObject("/org/freedesktop/FileManager1", &manager, QDBusConnection::ExportAllSlots));
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const QString file = dir.filePath("completed file.txt");
        QFile payload(file); QVERIFY(payload.open(QIODevice::WriteOnly)); payload.write("fixture"); payload.close();
        DesktopOpener opener;
        QVERIFY(opener.openFile(QUrl::fromLocalFile(file)));
        QTRY_COMPARE(portal.calls, 1);
        QCOMPARE(portal.path, file);
        QCOMPARE(portal.options.value("ask").toBool(), false);
        QCOMPARE(portal.options.value("writable").toBool(), false);
        QVERIFY(opener.showInFolder(QUrl::fromLocalFile(file)));
        QTRY_COMPARE(manager.items, 1);
        QCOMPARE(manager.urls, QStringList{QUrl::fromLocalFile(file).toString()});
        QVERIFY(opener.showInFolder(QUrl::fromLocalFile(dir.path())));
        QTRY_COMPARE(manager.folders, 1);
        QCOMPARE(manager.urls, QStringList{QUrl::fromLocalFile(dir.path()).toString()});
        // No filesystem-special objects, remote URLs, directories as files or relative paths.
        const QString fifo = dir.filePath("pipe");
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        for (const auto &url : {QUrl(), QUrl("https://example.invalid/a"), QUrl::fromLocalFile("relative"),
                               QUrl::fromLocalFile(fifo), QUrl::fromLocalFile(dir.filePath("missing")), QUrl::fromLocalFile(dir.path())})
            QVERIFY(!opener.openFile(url));
        QVERIFY(!opener.showInFolder(QUrl::fromLocalFile(fifo)));
        QTest::qWait(50); QCOMPARE(portal.calls, 1); QCOMPARE(manager.items, 1);
        // Download pages go to the session's browser through the portal, never a private xdg-open.
        QVERIFY(opener.openWebPage(QUrl("https://example.invalid/page?q=a b#part")));
        QTRY_COMPARE(portal.uris, 1);
        QCOMPARE(portal.uri, QStringLiteral("https://example.invalid/page?q=a%20b#part"));
        for (const auto &url : {QUrl(), QUrl("file:///etc/passwd"), QUrl("javascript:alert(1)"), QUrl("https://user:pw@example.invalid/"),
                               QUrl("http:///nohost")})
            QVERIFY(!opener.openWebPage(url));
        QTest::qWait(50); QCOMPARE(portal.uris, 1);
        bus.unregisterObject("/org/freedesktop/portal/desktop");
        bus.unregisterObject("/org/freedesktop/FileManager1");
        bus.unregisterService("org.freedesktop.portal.Desktop");
        bus.unregisterService("org.freedesktop.FileManager1");
    }
};
QTEST_MAIN(DesktopOpenerTest)
#include "tst_desktopopener.moc"
