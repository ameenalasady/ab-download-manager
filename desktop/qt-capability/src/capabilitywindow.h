#pragma once
#include <QQuickView>

// Ordinary decorated Wayland toplevel. KWin owns captions, shadows, corners,
// move/resize and title alignment; no client masks or resize-region updates.
class CapabilityWindow : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(bool blurRequested READ blurRequested NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool blurBridgeAvailable READ blurBridgeAvailable CONSTANT)
    Q_PROPERTY(bool reducedTransparency READ reducedTransparency NOTIFY diagnosticsChanged)
    Q_PROPERTY(bool kirigamiAvailable READ kirigamiAvailable CONSTANT)
    Q_PROPERTY(QString diagnosticsText READ diagnosticsText NOTIFY diagnosticsChanged)
public:
    explicit CapabilityWindow(QWindow *parent = nullptr);
    void setInitialState(bool blurEnabled, bool reducedTransparency);
    Q_INVOKABLE void setBlurEnabled(bool enabled);
    Q_INVOKABLE void setReducedTransparency(bool reduced);
    Q_INVOKABLE QRectF itemRectInWindow(const QString &name) const;
    bool saveFrame(const QString &path);
    bool runSelfTest(const QString &path);
    bool blurRequested() const { return m_blurRequested; }
    bool blurBridgeAvailable() const;
    bool reducedTransparency() const { return m_reducedTransparency; }
    bool kirigamiAvailable() const;
    QString diagnosticsText() const;
    QString quickStyleName() const;
    QString widgetStyleName() const;
    qreal windowOpacity() const { return opacity(); }
    int alphaBufferSize() const { return format().alphaBufferSize(); }
signals:
    void diagnosticsChanged();
protected:
    void showEvent(QShowEvent *event) override;
private:
    void applyBlur();
    bool m_blurRequested = false;
    bool m_reducedTransparency = false;
};
