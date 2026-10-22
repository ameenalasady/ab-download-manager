#include "desktopopener.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
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
