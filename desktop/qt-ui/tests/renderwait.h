#pragma once

#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest>

// Wait for an actual rendered frame, not a guessed style/rendering sleep. Capture
// galleries retain their extra animation-settling waits at their call sites.
inline bool waitForRenderedFrame(QQuickWindow *window)
{
    QSignalSpy frame(window, &QQuickWindow::frameSwapped);
    window->update();
    return QTest::qWaitFor([&] { return !frame.isEmpty(); }, 5000);
}
