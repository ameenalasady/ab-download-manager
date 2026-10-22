#pragma once
#include <QObject>
#include <QMenu>
#include <QSystemTrayIcon>

class QQuickView;
class QWindow;
class ServiceDownloads;
class DesktopAdapters : public QObject {
    Q_OBJECT
public:
    DesktopAdapters(QQuickView *window, ServiceDownloads *service);
    bool eventFilter(QObject *object, QEvent *event) override;
private:
    void synchronize();
    void activate(const QString &token = {}, QWindow *target = nullptr);
    void invoke(const char *method);
    QQuickView *m_window;
    ServiceDownloads *m_service;
    QMenu m_menu;
    QSystemTrayIcon m_tray;
    QAction *m_import, *m_speed, *m_settings, *m_exit;
};
