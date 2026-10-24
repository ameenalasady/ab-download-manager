#pragma once
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

// Opens completed downloads in the USER'S desktop session. The frontend runs with a private
// HOME/XDG tree, so Qt.openUrlExternally would spawn xdg-open in that environment (no
// mimeapps.list, no runtime sockets) and silently fail. Instead the session's own services
// are asked over D-Bus: the OpenURI portal for files, the file manager for "show in folder".
// Only local, absolute, existing paths are accepted; callers pass completedFileUrl() values.
class DesktopOpener : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    // Default application for the file. `window` (optional) provides an activation token so
    // the opened application may take focus on Wayland.
    Q_INVOKABLE bool openFile(const QUrl &url, QWindow *window = nullptr);
    // Containing folder in the file manager, with the file selected when supported.
    Q_INVOKABLE bool showInFolder(const QUrl &url, QWindow *window = nullptr);
    // A web page (http/https only, no embedded credentials) in the user's browser.
    Q_INVOKABLE bool openWebPage(const QUrl &url, QWindow *window = nullptr);
    // Folder chooser of the USER'S session (FileChooser portal), for the same private-HOME
    // reason as above. Returns a request token (empty if nothing could be started); the
    // answer arrives as folderChosen(token, path), path empty on cancel or an unusable pick.
    // `current` (optional) is the folder the chooser starts in.
    Q_INVOKABLE QString chooseFolder(const QString &title, const QString &current, QWindow *window = nullptr);
signals:
    void folderChosen(const QString &token, const QString &path);
};
