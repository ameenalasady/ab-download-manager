#include "previewdownloads.h"
#include "servicedownloads.h"
#include "desktopadapters.h"
#include "resizeprofile.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickView>
#include <QRegularExpression>
#include <QStyle>
#include <QSurfaceFormat>
#include <QTimer>
#include <QDebug>

#include <cstdio>
#include <memory>

namespace {

constexpr char kWaylandPlatform[] = "wayland";

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

} // namespace

int main(int argc, char **argv)
{
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
        + (tag.isEmpty() ? QString() : QLatin1Char('.') + tag);

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
    QGuiApplication::setDesktopFileName(tag == QLatin1String("abdm")
        ? QStringLiteral("com.abdownloadmanager") : identity);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("AB Download Manager"));

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QSurfaceFormat surfaceFormat;
    surfaceFormat.setAlphaBufferSize(8);
    // Never block a swap on the compositor's frame callback. During an interactive resize
    // KWin sends the next size only once the previous one is committed; Qt Wayland's
    // emulated blocking swap delayed every resized frame to the next callback, so edge
    // drags ran at about one size per refresh, behind Dolphin. Wayland presents whole
    // buffers (no tearing), and animations stay paced by frame callbacks.
    surfaceFormat.setSwapInterval(0);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);
    QQuickWindow::setDefaultAlphaBuffer(true);

    QApplication app(argc, argv);
    // Every toplevel (main, download, completion, browser review, dialogs) carries the app
    // icon via xdg-toplevel-icon, independent of whether a matching .desktop file exists.
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/icon.png")));
    if (!actualPlatformIsWayland()) {
        std::fprintf(stderr,
                     "qt-capability: actual Qt platform is '%s', not native wayland; refusing to create a window\n",
                     QGuiApplication::platformName().toLocal8Bit().constData());
        return 2;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("AB Download Manager Qt frontend; example data unless --service-descriptor is given."));
    parser.addHelpOption();
    parser.addVersionOption();

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
    const QCommandLineOption serviceOption(QStringLiteral("service-descriptor"),
        QStringLiteral("Connect to the Kotlin service through this descriptor."), QStringLiteral("path"));
    const QCommandLineOption resizeProfileOption(QStringLiteral("resize-profile"),
        QStringLiteral("Synthetic resize-to-frame probe, writes JSON and exits (example data only)."), QStringLiteral("path"));

    parser.addOption(QCommandLineOption(QStringLiteral("instance-tag"),
        QStringLiteral("Disposable app-ID suffix to avoid remembered geometry collisions."),
        QStringLiteral("tag")));
    parser.addOption(serviceOption);
    parser.addOption(resizeProfileOption);
    parser.addOption(widthOption);
    parser.addOption(heightOption);
    parser.addOption(styleOption);
    parser.process(app);

    if (!parser.positionalArguments().isEmpty()) {
        std::fprintf(stderr, "qt-capability: unexpected positional arguments; use --help\n");
        return 2;
    }

    bool widthOk = false;
    bool heightOk = false;
    const int width = parser.value(widthOption).toInt(&widthOk);
    const int height = parser.value(heightOption).toInt(&heightOk);
    if (!widthOk || !heightOk || width < 760 || height < 600) {
        std::fprintf(stderr, "qt-capability: --width and --height must be integers, at least 760x600\n");
        return 2;
    }
    if (parser.isSet(resizeProfileOption) && parser.isSet(serviceOption)) {
        std::fprintf(stderr, "qt-capability: --resize-profile never runs against a service\n");
        return 2;
    }

    QQuickStyle::setStyle(parser.value(styleOption));
    // Use the desktop application's configured font, not an application override.

    PreviewDownloads previewDownloads;
    std::unique_ptr<ServiceDownloads> serviceDownloads;
    if (parser.isSet(serviceOption)) serviceDownloads = std::make_unique<ServiceDownloads>(parser.value(serviceOption));
    QObject *downloads = serviceDownloads ? static_cast<QObject *>(serviceDownloads.get()) : &previewDownloads;

    // Ordinary decorated Wayland toplevel. KWin owns captions, shadows, corners and
    // move/resize; the view's NativeWindowStyle supplies the material and blur request.
    QQuickView window;
    window.setFlags(Qt::Window);
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setColor(Qt::transparent);
    window.setMinimumSize(QSize(760, 600));
    window.setTitle(serviceDownloads ? QStringLiteral("AB Download Manager")
                                     : QStringLiteral("AB Download Manager — interface preview (example data only)"));
    window.setInitialProperties({{QStringLiteral("downloads"), QVariant::fromValue(downloads)}});
    window.resize(width, height);

    window.setSource(QUrl(QStringLiteral("qrc:/qt-ui/qml/DownloadsView.qml")));
    if (window.status() == QQuickView::Error || !window.rootObject()) {
        for (const QQmlError &error : window.errors()) {
            qCritical().noquote() << "QML:" << error.toString();
        }
        qCritical() << "Downloads view failed to load; refusing an empty window";
        return 3;
    }
    qInfo().noquote() << "qt-capability"
                      << "platform=" + QGuiApplication::platformName()
                      << "identity=" + QGuiApplication::desktopFileName()
                      << "quick_style=" + QQuickStyle::name()
                      << "widget_style=" + QApplication::style()->name();

    std::unique_ptr<DesktopAdapters> desktopAdapters;
    if (serviceDownloads) desktopAdapters = std::make_unique<DesktopAdapters>(&window, serviceDownloads.get());
    window.show();
    if (parser.isSet(resizeProfileOption)) {
        profileResizing(&window, parser.value(resizeProfileOption));
        QTimer::singleShot(90000, &app, [] { QCoreApplication::exit(4); });
    }
    return app.exec();
}
