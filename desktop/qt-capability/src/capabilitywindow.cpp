#include "capabilitywindow.h"
#include "kwindowsystembridge.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QQuickItem>
#include <QQuickStyle>
#include <QStyle>
#include <QDebug>

CapabilityWindow::CapabilityWindow(QWindow *parent) : QQuickView(parent) {
    setFlags(Qt::Window);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setColor(Qt::transparent);
    setOpacity(1.0);
    setTitle(QStringLiteral("Qt Capability Shell (synthetic, isolated)"));
}
void CapabilityWindow::setInitialState(bool blurEnabled, bool reducedTransparency) {
    m_reducedTransparency = reducedTransparency;
    m_blurRequested = blurEnabled && !reducedTransparency && blurBridgeAvailable();
}
void CapabilityWindow::setBlurEnabled(bool enabled) {
    m_blurRequested = enabled && !m_reducedTransparency && blurBridgeAvailable();
    applyBlur(); emit diagnosticsChanged();
}
void CapabilityWindow::setReducedTransparency(bool reduced) {
    m_reducedTransparency = reduced;
    if (reduced) m_blurRequested = false;
    applyBlur(); emit diagnosticsChanged();
}
void CapabilityWindow::applyBlur() {
    // The real downloads view owns its native style material (and corresponding
    // blur policy); synthetic probe toggles must never override that request.
    if (rootObject() && rootObject()->property("migrationView").toBool()) return;
    // Empty region means the whole client surface; it follows size on the compositor.
    capability::requestBlurBehind(this, m_blurRequested && !m_reducedTransparency, QRegion());
}
void CapabilityWindow::showEvent(QShowEvent *event) {
    QQuickView::showEvent(event); applyBlur(); emit diagnosticsChanged();
}
bool CapabilityWindow::blurBridgeAvailable() const { return capability::kf6BlurBridgeBuilt(); }
bool CapabilityWindow::kirigamiAvailable() const {
    return QFileInfo(QLibraryInfo::path(QLibraryInfo::QmlImportsPath) + "/org/kde/kirigami/qmldir").isFile();
}
QString CapabilityWindow::quickStyleName() const { return QQuickStyle::name(); }
QString CapabilityWindow::widgetStyleName() const { return QApplication::style()->name(); }
QString CapabilityWindow::diagnosticsText() const {
    return QStringLiteral("platform: %1 (native wayland only)\nquick_style: %2\nwidget_style: %3\n"
        "decorations: server-side (title alignment controlled by KWin)\n"
        "opacity: %4; alpha_buffer: %5\nblur_bridge: %6; blur_requested: %7 (rendering unverified)\n"
        "no client shadow, mask, rounded layer or resize regions")
        .arg(QGuiApplication::platformName(), quickStyleName(), widgetStyleName())
        .arg(opacity()).arg(alphaBufferSize()).arg(blurBridgeAvailable()).arg(m_blurRequested);
}
QRectF CapabilityWindow::itemRectInWindow(const QString &name) const {
    const auto *item = rootObject() ? rootObject()->findChild<QQuickItem *>(name) : nullptr;
    return item ? item->mapRectToScene(QRectF(QPointF(), item->size())) : QRectF();
}
bool CapabilityWindow::saveFrame(const QString &path) {
    const auto image = grabWindow();
    QDir().mkpath(QFileInfo(path).absolutePath());
    return !image.isNull() && image.save(path);
}
bool CapabilityWindow::runSelfTest(const QString &path) {
    const bool ok = QGuiApplication::platformName() == "wayland"
        && !(flags() & Qt::FramelessWindowHint) && mask().isEmpty()
        && qFuzzyCompare(opacity(), 1.0) && alphaBufferSize() > 0
        && itemRectInWindow("shellBackground").toRect() == QRect(QPointF().toPoint(), size())
        && saveFrame(path);
    qInfo() << "SELFTEST RESULT" << (ok ? "PASS" : "FAIL");
    return ok;
}
