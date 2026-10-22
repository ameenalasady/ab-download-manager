#pragma once
#include <QObject>
#include <QColor>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

// Samples the active QWidget style's material; QML supplies its shared opacity ceiling.
// A full-client logical blur region supports compositors/plugins that do not
// implement the protocol's empty-region whole-surface convention.
class NativeWindowStyle : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(QColor background READ background NOTIFY backgroundChanged)
    Q_PROPERTY(qreal maximumOpacity READ maximumOpacity WRITE setMaximumOpacity NOTIFY backgroundChanged)
    Q_PROPERTY(bool blurAvailable READ blurAvailable CONSTANT)
public:
    explicit NativeWindowStyle(QObject *parent = nullptr);
    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    QColor background() const;
    qreal maximumOpacity() const { return m_maximumOpacity; }
    void setMaximumOpacity(qreal value);
    bool blurAvailable() const;
    Q_INVOKABLE void activate(const QString &token = {});
signals:
    void windowChanged();
    void backgroundChanged();
protected:
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void apply();
    QPointer<QWindow> m_window;
    QColor m_background;
    qreal m_maximumOpacity = 1.0;
};
