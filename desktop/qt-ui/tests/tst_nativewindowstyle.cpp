#include "nativewindowstyle.h"
#include <QQuickWindow>
#include <QtTest>

class NativeWindowStyleTest : public QObject
{
    Q_OBJECT
private slots:
    void secondarySurfaceGetsAlphaWithoutDependingOnHostDefault()
    {
        QQuickWindow::setDefaultAlphaBuffer(false);
        QQuickWindow window;
        NativeWindowStyle style;
        style.setMaximumOpacity(0.86);
        style.setWindow(&window);
        QCOMPARE(window.requestedFormat().alphaBufferSize(), 8);
        QVERIFY(style.background().alphaF() <= 0.861);
        window.setColor(style.background());
        window.resize(320, 200);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto id = window.winId();
        // Reattaching to an already mapped surface never recreates it or changes
        // native identity/placement, and still reapplies the blur request.
        style.setWindow(nullptr);
        style.setWindow(&window);
        QCOMPARE(window.winId(), id);
        QCOMPARE(window.opacity(), 1.0); // Never fade text or controls globally.
        QVERIFY(!window.grabWindow().isNull());
        window.hide();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.winId(), id);
        QQuickWindow::setDefaultAlphaBuffer(true);
    }
};
QTEST_MAIN(NativeWindowStyleTest)
#include "tst_nativewindowstyle.moc"
