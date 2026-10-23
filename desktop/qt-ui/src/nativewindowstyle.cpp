#include "nativewindowstyle.h"
#include "kwindowsystembridge.h"
#include <QApplication>
#include <QMainWindow>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QSurfaceFormat>
#include <QStyleOption>
#include <QTimer>
#include <limits>
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
#include <KWindowSystem>
#endif

NativeWindowStyle::NativeWindowStyle(QObject *parent) : QObject(parent) {
    // Polish exactly the top-level widget kind used by Dolphin. Kvantum decides
    // translucency (including opaque themes/app exceptions) during polish. Never
    // show it, create a platform surface, or read/write desktop configuration here.
    static const QColor material = [] {
        QMainWindow probe; probe.resize(400, 300); probe.ensurePolished();
        QImage image(probe.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
        QPainter painter(&image); QStyleOption option; option.initFrom(&probe); option.rect = probe.rect();
        probe.style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, &probe); painter.end();
        const QColor sample = image.pixelColor(200, 150);
        // Breeze/other styles can leave PE_Widget unpainted: use their Window role.
        return sample.alpha() > 0 ? sample : probe.palette().color(QPalette::Window);
    }();
    m_background = material;
}
QColor NativeWindowStyle::background() const {
    QColor color = m_background;
    color.setAlphaF(qMin(color.alphaF(), m_maximumOpacity));
    return color;
}
void NativeWindowStyle::setMaximumOpacity(qreal value) {
    value = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_maximumOpacity, value)) return;
    m_maximumOpacity = value;
    apply();
    emit backgroundChanged();
}
bool NativeWindowStyle::blurAvailable() const { return capability::kf6BlurBridgeBuilt(); }
void NativeWindowStyle::setWindow(QWindow *window) {
    if (window == m_window) return;
    if (m_window) m_window->removeEventFilter(this);
    m_window = window;
    if (m_window) {
        // QML toplevels and Popup.Window create their own platform surfaces. Do not
        // rely only on the host's default format (which belongs to the main view).
        // Request alpha before native creation; never destroy/recreate a live surface.
        if (!m_window->handle()) {
            auto format = m_window->requestedFormat();
            if (format.alphaBufferSize() < 8) {
                format.setAlphaBufferSize(8);
                m_window->setFormat(format);
            }
        }
        m_window->installEventFilter(this);
        if (m_window->isVisible()) apply();
    }
    emit windowChanged();
}
void NativeWindowStyle::apply() {
    if (!m_window) return;
    // An explicit, size-independent region covering the whole surface (the compositor
    // clips it to the window). On KWin >= 6.6.90 the installed better-blur-dx treats an
    // empty Wayland blur region as “no request”, so empty is not used; this is the same
    // rectangle KWindowEffects itself sends for "whole window" on ext_background_effect.
    // Being size-independent, it is never resent while resizing: a per-size region was
    // committed a frame behind the resized buffer, leaving the new edge unblurred, and
    // made the blur effect redo its work on every resize step. Server-side decoration
    // blur still belongs to the decoration.
    static const QRegion region(QRect(std::numeric_limits<int>::min() / 2, std::numeric_limits<int>::min() / 2,
                                      std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
    const bool requested = capability::requestBlurBehind(m_window, background().alpha() < 255, region);
    if (qEnvironmentVariableIsSet("QT_NATIVE_MATERIAL_DIAGNOSTICS"))
        qInfo() << "native material" << m_window->objectName() << m_window->size()
                << "alpha" << m_window->format().alphaBufferSize() << "blur-requested" << requested
                << "exposed" << m_window->isExposed() << "type" << m_window->type();
}
bool NativeWindowStyle::eventFilter(QObject *object, QEvent *event) {
    if (object == m_window && (event->type() == QEvent::Show
        || (event->type() == QEvent::Expose && m_window->isExposed())
        || (event->type() == QEvent::PlatformSurface
            && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated))) {
        // Popup.Window creates/reparents its native surface during show. The first
        // expose is the reliable mapped-surface boundary: surface-created/show alone
        // can precede Wayland's completed configure. Reapply there, and on recreation,
        // without opacity hacks, custom decorations or compositor-setting changes.
        apply();
        QTimer::singleShot(0, this, &NativeWindowStyle::apply);
    }
    return QObject::eventFilter(object, event);
}
void NativeWindowStyle::activate(const QString &token) {
    if (!m_window) return;
    if (m_window->windowState() == Qt::WindowMinimized) m_window->showNormal(); else m_window->show();
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
    if (!token.isEmpty()) KWindowSystem::setCurrentXdgActivationToken(token);
    KWindowSystem::activateWindow(m_window);
#else
    Q_UNUSED(token);
    m_window->requestActivate();
#endif
    // No move(), output coordinates, fabricated tokens, raise loops or input injection.
    if (!m_window->isActive()) m_window->alert(0);
}
