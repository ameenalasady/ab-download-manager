#include "desktopopener.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QApplication>
#include <QFileDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <unistd.h>
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
#include <KWaylandExtras>
#include <kwindowsystem_version.h>
#endif

namespace {
QString localPath(const QUrl &url) {
    if (!url.isLocalFile()) return {};
    const QString path = QDir::cleanPath(url.toLocalFile());
    if (!QDir::isAbsolutePath(path)) return {};
    // Only a real file or directory is ever handed to the session services: never a FIFO,
    // socket, device or dangling link, and never a relative path.
    const QFileInfo info(path);
    if (!info.exists() || (!info.isFile() && !info.isDir())) return {};
    return path;
}

// Runs `send` with an xdg-activation token when one can be obtained quickly, else without.
void withActivationToken(QWindow *window, QObject *context, std::function<void(const QString &)> send) {
#if defined(QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM) && KWINDOWSYSTEM_VERSION >= QT_VERSION_CHECK(6, 19, 0)
    if (window && QGuiApplication::platformName() == QLatin1String("wayland")) {
        auto done = std::make_shared<bool>(false);
        auto once = [done, send](const QString &token) { if (!*done) { *done = true; send(token); } };
        KWaylandExtras::xdgActivationToken(window, QString()).then(context, once);
        QTimer::singleShot(500, context, [once] { once({}); }); // Never wait on a missing compositor reply.
        return;
    }
#else
    Q_UNUSED(window);
#endif
    Q_UNUSED(context);
    send({});
}

QDBusMessage portalCall(const char *method, const QString &path, const QVariantMap &options) {
    const int fd = ::open(QFile::encodeName(path).constData(), O_PATH | O_CLOEXEC);
    if (fd < 0) return {};
    const QDBusUnixFileDescriptor descriptor(fd); // Duplicates; close ours.
    ::close(fd);
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.OpenURI"), QLatin1String(method));
    message << QString() << QVariant::fromValue(descriptor) << options;
    return message;
}

// Async call; `fallback` runs if the service is missing or refuses.
void callWithFallback(const QDBusMessage &message, QObject *context, std::function<void()> fallback) {
    if (message.type() != QDBusMessage::MethodCallMessage) { fallback(); return; }
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, fallback] {
        if (watcher->isError()) {
            qWarning().noquote() << "DesktopOpener:" << watcher->error().name() << watcher->error().message();
            fallback();
        }
        watcher->deleteLater();
    });
}
}

bool DesktopOpener::openFile(const QUrl &url, QWindow *window) {
    const QString path = localPath(url);
    if (path.isEmpty() || !QFileInfo(path).isFile()) return false;
    withActivationToken(window, this, [this, path](const QString &token) {
        QVariantMap options{{QStringLiteral("ask"), false}, {QStringLiteral("writable"), false}};
        if (!token.isEmpty()) options.insert(QStringLiteral("activation_token"), token);
        callWithFallback(portalCall("OpenFile", path, options), this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    });
    return true;
}

bool DesktopOpener::showInFolder(const QUrl &url, QWindow *window) {
    const QString path = localPath(url);
    const QFileInfo info(path);
    if (path.isEmpty() || (!info.isFile() && !info.isDir())) return false;
    withActivationToken(window, this, [this, path](const QString &token) {
        // A folder itself opens in the file manager; a file is revealed and selected.
        const bool folder = QFileInfo(path).isDir();
        auto show = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
            QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1"),
            folder ? QStringLiteral("ShowFolders") : QStringLiteral("ShowItems"));
        show << QStringList{QUrl::fromLocalFile(path).toString()} << token;
        callWithFallback(show, this, [this, path, token] {
            QVariantMap options;
            if (!token.isEmpty()) options.insert(QStringLiteral("activation_token"), token);
            callWithFallback(portalCall("OpenDirectory", path, options), this, [path] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath()));
            });
        });
    });
    return true;
}

bool DesktopOpener::openWebPage(const QUrl &url, QWindow *window) {
    if (!url.isValid() || !QStringList{QStringLiteral("http"), QStringLiteral("https")}.contains(url.scheme())
        || url.host().isEmpty() || !url.userInfo().isEmpty()) return false;
    withActivationToken(window, this, [this, url](const QString &token) {
        QVariantMap options;
        if (!token.isEmpty()) options.insert(QStringLiteral("activation_token"), token);
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.OpenURI"), QStringLiteral("OpenURI"));
        message << QString() << url.toString(QUrl::FullyEncoded) << options;
        callWithFallback(message, this, [url] { QDesktopServices::openUrl(url); });
    });
    return true;
}

namespace {
// Receives the single Response of one portal request, then deletes itself.
class FolderResponse : public QObject {
    Q_OBJECT
public:
    FolderResponse(const QString &token, QObject *parent) : QObject(parent), token(token) {}
    QString token;
signals:
    void finished(const QString &token, const QString &path);
public slots:
    void Response(uint code, const QVariantMap &results) {
        QString path;
        const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
        if (code == 0 && !uris.isEmpty()) {
            const QUrl url(uris.first());
            if (url.isLocalFile()) {
                const QString clean = QDir::cleanPath(url.toLocalFile());
                if (QDir::isAbsolutePath(clean) && QFileInfo(clean).isDir()) path = clean;
            }
        }
        emit finished(token, path);
        deleteLater();
    }
};
}

QString DesktopOpener::chooseFolder(const QString &title, const QString &current, QWindow *) {
    static int counter = 0;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return {};
    const QString token = QStringLiteral("abdm%1").arg(++counter);
    QString sender = bus.baseService();
    sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    // Subscribe before the call so a fast answer cannot be missed.
    auto *response = new FolderResponse(token, this);
    bus.connect(QString(), requestPath, QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
        response, SLOT(Response(uint, QVariantMap)));
    connect(response, &FolderResponse::finished, this, &DesktopOpener::folderChosen);
    QVariantMap options{{QStringLiteral("handle_token"), token}, {QStringLiteral("directory"), true}, {QStringLiteral("modal"), true}};
    const QString start = QDir::cleanPath(current.trimmed());
    if (QDir::isAbsolutePath(start) && QFileInfo(start).isDir())
        options.insert(QStringLiteral("current_folder"), QFile::encodeName(start) + '\0');
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"), QStringLiteral("org.freedesktop.portal.FileChooser"), QStringLiteral("OpenFile"));
    message << QString() << title << options;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, response, token, title, start] {
        if (watcher->isError()) {
            qWarning().noquote() << "DesktopOpener:" << watcher->error().name() << watcher->error().message();
            response->deleteLater();
            QTimer::singleShot(0, this, [this, token, title, start] {
                QString path;
                if (qobject_cast<QApplication *>(QCoreApplication::instance()))
                    path = QFileDialog::getExistingDirectory(nullptr, title, start);
                emit folderChosen(token, path);
            });
        }
        watcher->deleteLater();
    });
    return token;
}

#include "desktopopener.moc"
