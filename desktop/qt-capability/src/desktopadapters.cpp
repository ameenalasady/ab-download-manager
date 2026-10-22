#include "desktopadapters.h"
#include "servicedownloads.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QIcon>
#include <QQuickItem>
#include <QQuickView>
#include <QTimer>
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
#include <KWindowSystem>
#endif

DesktopAdapters::DesktopAdapters(QQuickView *window, ServiceDownloads *service)
    : QObject(window), m_window(window), m_service(service) {
    auto *show = m_menu.addAction(tr("Show Downloads"));
    m_import = m_menu.addAction(tr("New Download from Clipboard"));
    m_speed = m_menu.addAction(tr("Speed Limiter")); m_speed->setCheckable(true);
    m_menu.addSeparator(); m_settings = m_menu.addAction(tr("Settings…"));
    m_exit = m_menu.addAction(tr("Exit and Stop Service"));
    m_tray.setIcon(QGuiApplication::windowIcon().isNull()
        ? QIcon::fromTheme("download", QIcon::fromTheme("folder-downloads")) : QGuiApplication::windowIcon());
    m_tray.setToolTip(tr("AB Download Manager")); m_tray.setContextMenu(&m_menu);
    connect(show, &QAction::triggered, this, [this] { activate(); });
    connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) activate();
    });
    connect(m_import, &QAction::triggered, service, &ServiceDownloads::importFromClipboard);
    connect(service, &ServiceDownloads::clipboardImportRequested, this, [this] {
        // Explicit user gesture only. Never subscribe to clipboard changes or log its text.
        activate(); m_service->importLinks(QGuiApplication::clipboard()->text(QClipboard::Clipboard));
    });
    connect(m_speed, &QAction::triggered, this, [this](bool enabled) {
        m_service->updateSettings({{"useSpeedLimit", enabled ? "Enabled" : "Disabled"}});
    });
    connect(m_settings, &QAction::triggered, this, [this] { activate(); invoke("openSettings"); });
    // Same confirmation as File → Exit while downloads or queues are running.
    connect(m_exit, &QAction::triggered, this, [this] { activate(); invoke("requestExit"); });
    connect(service, &ServiceDownloads::serviceStopped, qApp, &QCoreApplication::quit);
    connect(service, &ServiceDownloads::activationRequested, this,
        [this](const QString &token, const QString &action, const QString &downloadId) {
            if (action == "show" && !downloadId.isEmpty()) {
                // A download notification opens that download's own window, not the list.
                QVariant shown;
                if (auto *root = m_window->rootObject())
                    if (auto *downloads = root->findChild<QObject *>("downloadsPreview"))
                        QMetaObject::invokeMethod(downloads, "showDownloadWindow", Q_RETURN_ARG(QVariant, shown),
                                                  Q_ARG(QString, downloadId), Q_ARG(bool, false));
                if (auto *target = qobject_cast<QWindow *>(shown.value<QObject *>())) { activate(token, target); return; }
            }
            if (action == "review") {
                // Route the real notification token to the independent review window,
                // never unhide/activate the main list first (which would consume it).
                QVariant shown;
                if (auto *root = m_window->rootObject())
                    if (auto *downloads = root->findChild<QObject *>("downloadsPreview"))
                        QMetaObject::invokeMethod(downloads, "activateBrowserReview", Q_RETURN_ARG(QVariant, shown), Q_ARG(QString, token));
                return;
            }
            activate(token);
            if (!downloadId.isEmpty()) { m_service->setCategory("all"); m_service->setSearch({}); m_service->select(downloadId); }
        });
    // Any of this frontend's windows (list or download window) counts as foreground.
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this] { m_service->setFrontendActive(QGuiApplication::focusWindow() != nullptr); });
    connect(service, &ServiceDownloads::viewChanged, this, &DesktopAdapters::synchronize);
    connect(service, &ServiceDownloads::selectionChanged, this, &DesktopAdapters::synchronize);
    window->installEventFilter(this);
    // Plasma's tray watcher can appear after the frontend; Qt retries registration itself.
    auto *watcher = new QTimer(this); watcher->setInterval(2000);
    connect(watcher, &QTimer::timeout, this, &DesktopAdapters::synchronize); watcher->start();
    synchronize();
}

void DesktopAdapters::synchronize() {
    const bool explicitRuntime = m_service->runtime().value("mode") == "explicit-runtime";
    const bool tray = explicitRuntime && m_service->integration().value("useSystemTray", false).toBool()
        && QSystemTrayIcon::isSystemTrayAvailable();
    if (tray != m_tray.isVisible()) {
        m_tray.setVisible(tray);
        // With a tray, closing the last download window while the list is hidden must not
        // quit the frontend. Without one, the frontend exits after its last window closes.
        QGuiApplication::setQuitOnLastWindowClosed(!tray);
        // Disabling the tray while hidden must never strand the only frontend.
        if (!tray && !m_window->isVisible()) m_window->show();
    }
    // A pending power action countdown must be seen: bring the list window forward once.
    const QString powerSerial = explicitRuntime && m_service->connectionState() == "ready"
        && m_service->capabilities().contains("queue.powerAction")
        ? m_service->powerAction().value("serial").toString() : QString();
    if (!powerSerial.isEmpty() && powerSerial != m_powerSerial) activate();
    m_powerSerial = powerSerial;
    m_import->setEnabled(explicitRuntime && !m_service->busy() && m_service->capabilities().contains("links.import"));
    m_speed->setEnabled(!m_service->busy());
    m_speed->setChecked(m_service->settings().value("useSpeedLimit") != "Disabled");
    m_settings->setEnabled(!m_service->busy()); m_exit->setEnabled(!m_service->busy());
}

void DesktopAdapters::activate(const QString &token, QWindow *target) {
    QWindow *window = target ? target : m_window;
    if (window->windowState() == Qt::WindowMinimized) window->showNormal(); else window->show();
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
    if (!token.isEmpty()) KWindowSystem::setCurrentXdgActivationToken(token);
    KWindowSystem::activateWindow(window);
#else
    Q_UNUSED(token);
    window->requestActivate();
#endif
    // No focus-stealing workaround: the compositor may refuse a tokenless request.
    if (!window->isActive()) window->alert(0);
}
void DesktopAdapters::invoke(const char *method) {
    if (auto *root = m_window->rootObject())
        if (auto *downloads = root->findChild<QObject *>("downloadsPreview")) QMetaObject::invokeMethod(downloads, method);
}
bool DesktopAdapters::eventFilter(QObject *object, QEvent *event) {
    if (object == m_window && event->type() == QEvent::Close && m_tray.isVisible()) {
        static_cast<QCloseEvent *>(event)->ignore(); m_window->hide();
        m_service->setFrontendActive(QGuiApplication::focusWindow() != nullptr); return true;
    }
    return QObject::eventFilter(object, event);
}
