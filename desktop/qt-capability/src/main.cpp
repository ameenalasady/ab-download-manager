#include "capabilitywindow.h"
#include "previewdownloads.h"
#include "servicedownloads.h"
#include "desktopadapters.h"
#include "resizeprofile.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QRegularExpression>
#include <QSurfaceFormat>
#include <QTimer>
#include <QDebug>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

constexpr char kWaylandPlatform[] = "wayland";

bool argumentPresent(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

bool preparePlatformEnvironment()
{
    const QByteArray configuredPlatform = qgetenv("QT_QPA_PLATFORM");
    if (!configuredPlatform.isEmpty() && configuredPlatform != kWaylandPlatform) {
        std::fprintf(stderr,
                     "qt-capability: refusing QT_QPA_PLATFORM=%s; only native wayland is supported\n",
                     configuredPlatform.constData());
        return false;
    }

    // Set this before constructing QApplication. There is deliberately no X11
    // fallback path anywhere in this program.
    qputenv("QT_QPA_PLATFORM", kWaylandPlatform);
    return true;
}

bool actualPlatformIsWayland()
{
    return QGuiApplication::platformName() == QLatin1String(kWaylandPlatform);
}

bool saveViewFrame(QQuickView *view, const QString &path)
{
    const QImage image = view->grabWindow();
    if (image.isNull()) {
        qWarning() << "FRAME_SAVED status=failed reason=null-image";
        return false;
    }
    const QFileInfo info(path);
    if (!info.absolutePath().isEmpty()) {
        QDir().mkpath(info.absolutePath());
    }
    if (!image.save(path)) {
        qWarning().noquote() << "FRAME_SAVED status=failed path=" << path;
        return false;
    }
    qInfo().noquote() << "FRAME_SAVED status=ok"
                      << "path=" + path
                      << QStringLiteral("size=%1x%2").arg(image.width()).arg(image.height())
                      << QStringLiteral("has_alpha=%1").arg(image.hasAlphaChannel() ? "true" : "false")
                      << "note=alpha-evidence-not-compositor-blur-proof";
    return true;
}

void afterFirstFrame(QQuickView *view, std::function<void()> action)
{
    auto captured = std::make_shared<bool>(false);
    auto runOnce = [captured, action = std::move(action)] {
        if (*captured) {
            return;
        }
        *captured = true;
        action();
    };

    QObject::connect(view, &QQuickWindow::frameSwapped, view, [view, runOnce] {
        QTimer::singleShot(300, view, runOnce);
    });
    // Do not hang forever if the compositor never swaps a frame. The action
    // itself decides whether the resulting evidence is sufficient.
    QTimer::singleShot(4000, view, runOnce);
}

QString defaultFramePath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("qt-capability-selftest.png"));
}

QColor backdropClearColor(const QString &pattern)
{
    if (pattern == QLatin1String("black")) {
        return QColor(0, 0, 0, 255);
    }
    if (pattern == QLatin1String("white")) {
        return QColor(255, 255, 255, 255);
    }
    if (pattern == QLatin1String("magenta")) {
        return QColor(255, 0, 255, 255);
    }
    return QColor(14, 14, 20, 255);
}

} // namespace

int main(int argc, char **argv)
{
    const bool backdropHint = argumentPresent(argc, argv, "--backdrop");
    QString tag;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QLatin1String("--instance-tag") && i + 1 < argc) {
            tag = QString::fromLocal8Bit(argv[++i]);
        } else if (argument.startsWith(QLatin1String("--instance-tag="))) {
            tag = argument.mid(15);
        }
    }
    if (!tag.isEmpty() && !QRegularExpression(QStringLiteral("^[a-z0-9-]{1,48}$")).match(tag).hasMatch()) {
        std::fprintf(stderr, "qt-capability: invalid disposable instance tag\n");
        return 2;
    }
    const QString identity = QStringLiteral("org.abdownloadmanager.QtCapability")
        + (tag.isEmpty() ? QString() : QLatin1Char('.') + tag)
        + (backdropHint ? QStringLiteral(".Backdrop") : QString());

    if (!preparePlatformEnvironment()) {
        return 2;
    }

    QCoreApplication::setOrganizationName(QStringLiteral("QtCapability"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("abdownloadmanager.org"));
    QCoreApplication::setApplicationName(identity);
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));
    // The packaged instance ("abdm") is the installed application: use the installed
    // entry's ID (com.abdownloadmanager.desktop) as the Wayland app ID, so KWin, the task
    // manager, notifications and portals resolve its name and icon. Other tags stay distinct.
    QGuiApplication::setDesktopFileName(tag == QLatin1String("abdm") && !backdropHint
        ? QStringLiteral("com.abdownloadmanager") : identity);
    QGuiApplication::setApplicationDisplayName(
        backdropHint ? QStringLiteral("Qt Capability Backdrop (opaque, synthetic)")
        : argumentPresent(argc, argv, "--service-descriptor") ? QStringLiteral("AB Download Manager")
                     : QStringLiteral("Qt Capability Shell (synthetic)"));

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QSurfaceFormat surfaceFormat;
    surfaceFormat.setAlphaBufferSize(backdropHint ? 0 : 8);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);
    QQuickWindow::setDefaultAlphaBuffer(!backdropHint);

    QApplication app(argc, argv);
    // Every toplevel (main, download, completion, browser review, dialogs) carries the app
    // icon via xdg-toplevel-icon, independent of whether a matching .desktop file exists.
    if (!backdropHint) QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icon.png")));
    if (!actualPlatformIsWayland()) {
        std::fprintf(stderr,
                     "qt-capability: actual Qt platform is '%s', not native wayland; refusing to create a window\n",
                     QGuiApplication::platformName().toLocal8Bit().constData());
        return 2;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Qt6 Wayland capability shell and migration UI; optional private isolated-service adapter."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption backdropOption(
        QStringLiteral("backdrop"),
        QStringLiteral("Run the opaque backdrop process; no blur and no shell UI."));
    const QCommandLineOption backdropPatternOption(
        QStringLiteral("backdrop-pattern"),
        QStringLiteral("Opaque backdrop pattern: stripes, black, white, or magenta."),
        QStringLiteral("pattern"),
        QStringLiteral("stripes"));
    const QCommandLineOption selfTestOption(
        QStringLiteral("self-test"),
        QStringLiteral("Run programmatic geometry/alpha checks, save a frame, then exit."));
    const QCommandLineOption testDelayOption(
        QStringLiteral("self-test-delay-ms"),
        QStringLiteral("Delay the self-test for supervised geometry checks."),
        QStringLiteral("ms"), QStringLiteral("0"));
    const QCommandLineOption quitAfterOption(
        QStringLiteral("quit-after-ms"),
        QStringLiteral("Quit after the given number of milliseconds."),
        QStringLiteral("ms"));
    const QCommandLineOption frameOutOption(
        QStringLiteral("frame-out"),
        QStringLiteral("Save a client-side framebuffer PNG to this path."),
        QStringLiteral("path"));
    const QCommandLineOption widthOption(
        QStringLiteral("width"), QStringLiteral("Logical window width."), QStringLiteral("px"),
        QStringLiteral("980"));
    const QCommandLineOption heightOption(
        QStringLiteral("height"), QStringLiteral("Logical window height."), QStringLiteral("px"),
        QStringLiteral("840"));
    const QCommandLineOption styleOption(
        QStringLiteral("controls-style"),
        QStringLiteral("Qt Quick Controls style; default is org.kde.desktop."),
        QStringLiteral("name"), QStringLiteral("org.kde.desktop"));
    const QCommandLineOption noBlurOption(
        QStringLiteral("no-blur"), QStringLiteral("Start with the blur request disabled."));
    const QCommandLineOption opaqueOption(
        QStringLiteral("opaque"),
        QStringLiteral("Start in reduced-transparency/opaque mode; blur is disabled."));

    parser.addOption(QCommandLineOption(QStringLiteral("instance-tag"),
        QStringLiteral("Disposable app-ID suffix to avoid remembered geometry collisions."),
        QStringLiteral("tag")));
    const QCommandLineOption viewOption(QStringLiteral("view"),
        QStringLiteral("Presentation: capability or downloads (fixture preview unless --service-descriptor is supplied)."),
        QStringLiteral("name"), QStringLiteral("capability"));
    const QCommandLineOption serviceOption(QStringLiteral("service-descriptor"),
        QStringLiteral("Connect downloads view to the private isolated Kotlin service descriptor."), QStringLiteral("path"));
    parser.addOption(QCommandLineOption(QStringLiteral("resize-profile"),
        QStringLiteral("Synthetic resize-to-frame probe, writes JSON and exits (preview only)."), QStringLiteral("path")));
    parser.addOption(serviceOption);
    parser.addOption(viewOption);
    parser.addOption(backdropOption);
    parser.addOption(backdropPatternOption);
    parser.addOption(selfTestOption);
    parser.addOption(testDelayOption);
    parser.addOption(QCommandLineOption(QStringLiteral("probe-view"),
        QStringLiteral("Open a synthetic menu, dialog, or kirigami dialog for validation."),
        QStringLiteral("name"), QStringLiteral("none")));
    parser.addOption(quitAfterOption);
    parser.addOption(frameOutOption);
    parser.addOption(widthOption);
    parser.addOption(heightOption);
    parser.addOption(styleOption);
    parser.addOption(noBlurOption);
    parser.addOption(opaqueOption);
    parser.process(app);

    if (!parser.positionalArguments().isEmpty()) {
        std::fprintf(stderr, "qt-capability: unexpected positional arguments; use --help\n");
        return 2;
    }

    bool widthOk = false;
    bool heightOk = false;
    const int width = parser.value(widthOption).toInt(&widthOk);
    const int height = parser.value(heightOption).toInt(&heightOk);
    if (!widthOk || !heightOk || width < 200 || height < 200) {
        std::fprintf(stderr, "qt-capability: --width and --height must be integers >= 200\n");
        return 2;
    }

    int quitAfterMs = 0;
    if (parser.isSet(quitAfterOption)) {
        bool quitOk = false;
        quitAfterMs = parser.value(quitAfterOption).toInt(&quitOk);
        if (!quitOk || quitAfterMs <= 0) {
            std::fprintf(stderr, "qt-capability: --quit-after-ms must be a positive integer\n");
            return 2;
        }
    }

    bool delayOk = false;
    const int testDelay = parser.value(testDelayOption).toInt(&delayOk);
    if (!delayOk || testDelay < 0 || testDelay > 10000) {
        std::fprintf(stderr, "qt-capability: --self-test-delay-ms must be 0..10000\n");
        return 2;
    }

    const bool backdrop = parser.isSet(backdropOption) || backdropHint;
    const bool selfTest = parser.isSet(selfTestOption);
    const QString probeView = parser.value(QStringLiteral("probe-view"));
    if (!QStringList{QStringLiteral("none"), QStringLiteral("menu"),
                     QStringLiteral("dialog"), QStringLiteral("kirigami")}.contains(probeView)) {
        std::fprintf(stderr, "qt-capability: unknown --probe-view\n");
        return 2;
    }
    const QString viewName = parser.value(viewOption);
    if (viewName != QLatin1String("capability") && viewName != QLatin1String("downloads")) {
        std::fprintf(stderr, "qt-capability: --view must be capability or downloads\n");
        return 2;
    }
    const bool downloadsView = viewName == QLatin1String("downloads");
    if (parser.isSet(serviceOption) && !downloadsView) {
        qCritical() << "--service-descriptor requires --view downloads";
        return 2;
    }
    if (downloadsView && (backdrop || selfTest || probeView != QLatin1String("none")
                          || width < 760 || height < 600)) {
        std::fprintf(stderr, "qt-capability: downloads preview requires >=760x600, no --backdrop, --self-test or --probe-view\n");
        return 2;
    }
    const bool opaque = parser.isSet(opaqueOption);
    const bool noBlur = parser.isSet(noBlurOption);
    const QString frameOut = parser.value(frameOutOption);
    const QString style = parser.value(styleOption);

    const QStringList allowedBackdropPatterns{
        QStringLiteral("stripes"), QStringLiteral("black"), QStringLiteral("white"),
        QStringLiteral("magenta")};
    const QString backdropPattern = parser.value(backdropPatternOption).trimmed().toLower();
    if (!allowedBackdropPatterns.contains(backdropPattern)) {
        std::fprintf(stderr,
                     "qt-capability: --backdrop-pattern must be one of: stripes, black, white, magenta\n");
        return 2;
    }
    if (!backdrop && parser.isSet(backdropPatternOption)) {
        std::fprintf(stderr, "qt-capability: --backdrop-pattern requires --backdrop\n");
        return 2;
    }

    if (selfTest && backdrop) {
        std::fprintf(stderr, "qt-capability: --self-test applies to the shell, not --backdrop\n");
        return 2;
    }
    if (selfTest && quitAfterMs > 0) {
        qWarning() << "qt-capability: --quit-after-ms is ignored during --self-test";
    }

    if (backdrop) {
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setColor(backdropClearColor(backdropPattern));
        view.setOpacity(1.0);
        view.resize(width, height);
        view.setTitle(QStringLiteral("Qt Capability Backdrop (opaque, synthetic)"));

        qInfo().noquote() << "qt-capability mode=backdrop"
                          << "platform=" + QGuiApplication::platformName()
                          << "identity=" + QGuiApplication::desktopFileName()
                          << "opacity=" + QString::number(view.opacity(), 'f', 3)
                          << "pattern=" + backdropPattern
                          << "blur=none"
                          << "note=opaque-public-backdrop";

        view.setSource(QUrl(QStringLiteral("qrc:/qml/Backdrop.qml")));
        if (view.status() == QQuickView::Error) {
            for (const QQmlError &error : view.errors()) {
                qCritical().noquote() << "QML:" << error.toString();
            }
            return 3;
        }
        if (view.rootObject()) {
            view.rootObject()->setProperty("pattern", backdropPattern);
        } else {
            qWarning() << "qt-capability: backdrop root object unavailable";
        }

        view.show();
        if (!frameOut.isEmpty()) {
            afterFirstFrame(&view, [&view, frameOut] { saveViewFrame(&view, frameOut); });
        }
        if (quitAfterMs > 0) {
            QTimer::singleShot(quitAfterMs, &app, &QCoreApplication::quit);
        }
        return app.exec();
    }

    QQuickStyle::setStyle(style);
    // Use the desktop application's configured font, not an application override.

    PreviewDownloads previewDownloads;
    std::unique_ptr<ServiceDownloads> serviceDownloads;
    if (parser.isSet(serviceOption)) serviceDownloads = std::make_unique<ServiceDownloads>(parser.value(serviceOption));
    CapabilityWindow window;
    window.rootContext()->setContextProperty(QStringLiteral("controller"), &window);
    window.rootContext()->setContextProperty(QStringLiteral("previewDownloads"),
        serviceDownloads ? static_cast<QObject *>(serviceDownloads.get()) : static_cast<QObject *>(&previewDownloads));
    window.setInitialProperties({{QStringLiteral("migrationView"), downloadsView}});
    if (downloadsView) {
        window.setMinimumSize(QSize(760, 600));
        window.setTitle(serviceDownloads ? QStringLiteral("AB Download Manager")
                                        : QStringLiteral("AB Download Manager — interface preview (example data only)"));
    }
    window.setInitialState(!noBlur && !downloadsView, opaque);
    window.resize(width, height);

    qInfo().noquote() << "qt-capability mode=shell"
                      << "platform=" + QGuiApplication::platformName()
                      << "identity=" + QGuiApplication::desktopFileName()
                      << "quick_style=" + window.quickStyleName()
                      << "widget_style=" + window.widgetStyleName()
                      << "opacity=" + QString::number(window.windowOpacity(), 'f', 3)
                      << "alpha_buffer=" + QString::number(window.alphaBufferSize())
                      << "blur_bridge=" + QString(window.blurBridgeAvailable() ? "built" : "absent")
                      << "view=" + viewName
                      << "note=blur-rendering-unverified";

    window.setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (window.status() == QQuickView::Error) {
        for (const QQmlError &error : window.errors()) {
            qCritical().noquote() << "QML:" << error.toString();
        }
        return 3;
    }

    if (downloadsView && (!window.rootObject()
                         || !window.rootObject()->property("migrationReady").toBool())) {
        qCritical() << "Migration preview failed to load; refusing an empty shell";
        return 3;
    }

    if (selfTest || !frameOut.isEmpty()) {
        afterFirstFrame(&window, [&window, &app, selfTest, frameOut, testDelay] {
          QTimer::singleShot(testDelay, &window, [&window, &app, selfTest, frameOut] {
            qInfo().noquote() << "DIAGNOSTICS\n" + window.diagnosticsText();
            if (selfTest) {
                const QString path = frameOut.isEmpty() ? defaultFramePath() : frameOut;
                const bool ok = window.runSelfTest(path);
                app.exit(ok ? 0 : 3);
            } else {
                window.saveFrame(frameOut);
            }
          });
        });
    }

    if (selfTest) {
        QTimer::singleShot(8000 + testDelay, &app, [&app] {
            qCritical() << "SELFTEST RESULT FAIL reason=timeout";
            app.exit(4);
        });
    } else if (quitAfterMs > 0) {
        QTimer::singleShot(quitAfterMs, &app, &QCoreApplication::quit);
    }

    std::unique_ptr<DesktopAdapters> desktopAdapters;
    if (serviceDownloads) desktopAdapters = std::make_unique<DesktopAdapters>(&window, serviceDownloads.get());
    window.show();
    if (parser.isSet("resize-profile")) {
        if (serviceDownloads) return 2; // Never profile against a user's service.
        profileResizing(&window, parser.value("resize-profile"));
        QTimer::singleShot(90000, &app, [] { QCoreApplication::exit(4); });
    }
    if (probeView != QLatin1String("none")) {
        QTimer::singleShot(100, &window, [&window, probeView] {
            QMetaObject::invokeMethod(window.rootObject(), "openProbe", Q_ARG(QVariant, probeView));
        });
    }
    return app.exec();
}
