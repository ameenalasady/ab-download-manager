import QtQuick
import org.kde.kwin
Item {
    Timer {
        interval: 300; repeat: true; running: true
        onTriggered: {
            let backdrop = null, app = null
            for (const w of Workspace.windows) {
                if (!w.normalWindow) continue
                if (w.resourceClass.indexOf("Backdrop") >= 0) backdrop = w
                else if (w.resourceClass.indexOf("QtCapability") >= 0) app = w
            }
            if (!backdrop || !app) return
            running = false
            backdrop.frameGeometry = Qt.rect(0, 0, 1500, 900)
            app.frameGeometry = Qt.rect(300, 200, 800, 480)
            app.keepAbove = true
            Workspace.activeWindow = app
            console.warn("ABDM-PLACED " + JSON.stringify(app.frameGeometry) + " client " + JSON.stringify(app.clientGeometry))
        }
    }
}
