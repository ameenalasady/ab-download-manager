#include "previewdownloads.h"
#include <QQuickView>
#include <QColor>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QScopedPointer>
#include <QtTest>

class MockController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int shadowMargin READ shadowMargin CONSTANT)
    Q_PROPERTY(qreal cornerRadius READ cornerRadius CONSTANT)
    Q_PROPERTY(bool maximized READ maximized WRITE setMaximized NOTIFY maximizedChanged)
    Q_PROPERTY(bool blurRequested READ blurRequested CONSTANT)
    Q_PROPERTY(bool blurBridgeAvailable READ blurBridgeAvailable CONSTANT)
    Q_PROPERTY(bool reducedTransparency READ reducedTransparency CONSTANT)
    Q_PROPERTY(bool kirigamiAvailable READ kirigamiAvailable CONSTANT)
    Q_PROPERTY(QString diagnosticsText READ diagnosticsText CONSTANT)

public:
    int shadowMargin() const { return 16; }
    qreal cornerRadius() const { return 18.0; }
    bool maximized() const { return m_maximized; }
    void setMaximized(bool value) { m_maximized = value; emit maximizedChanged(); }
    bool blurRequested() const { return false; }
    bool blurBridgeAvailable() const { return true; }
    bool reducedTransparency() const { return false; }
    bool kirigamiAvailable() const { return false; }
    QString diagnosticsText() const { return QStringLiteral("synthetic diagnostics"); }

    Q_INVOKABLE void startSystemMoveFromTitleBar() {}
    Q_INVOKABLE void startSystemResize(int edges) { Q_UNUSED(edges); }
    Q_INVOKABLE void toggleMaximize() {}
    Q_INVOKABLE void setBlurEnabled(bool enabled) { Q_UNUSED(enabled); }
    Q_INVOKABLE void setReducedTransparency(bool reduced) { Q_UNUSED(reduced); }
    Q_INVOKABLE void requestClose() {}
signals:
    void maximizedChanged();
private:
    bool m_maximized = false;
};

class QmlLoadTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void mainShellQmlLoads();
    void migrationViewLoadsInNativeHost();
    void backdropQmlLoads();
    void optionalKirigamiExampleCompiles();
};

void QmlLoadTest::initTestCase()
{
    // Match the capability shell so the offscreen load covers the intended style.
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
}

void QmlLoadTest::mainShellQmlLoads()
{
    MockController controller;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("objectName").toString(), QStringLiteral("capabilityRoot"));
    QVERIFY(object->findChild<QObject *>(QStringLiteral("diagnosticsText")));
    QVERIFY(object->findChild<QObject *>(QStringLiteral("translucentMeasurementRegion")));
    QVERIFY(object->findChild<QObject *>(QStringLiteral("opaqueSwatch")));
    QVERIFY(object->findChild<QObject *>(QStringLiteral("syntheticRow1")));
    for (const QString &name : {QStringLiteral("menuButton"), QStringLiteral("blurButton"), QStringLiteral("opaqueButton")}) {
        auto *button = object->findChild<QQuickItem *>(name);
        QVERIFY(button);
        QVERIFY(button->implicitWidth() > 0);
        QVERIFY(button->implicitHeight() > 0);
    }
    auto *root = qobject_cast<QQuickItem *>(object.data());
    QVERIFY(root);
    root->setSize(QSizeF(980, 840));
    auto *shell = object->findChild<QQuickItem *>(QStringLiteral("shellBackground"));
    QVERIFY(shell);
    QVERIFY(!object->findChild<QObject *>("titleBar"));
    QVERIFY(!object->findChild<QObject *>("maximizeButton"));
    QVERIFY(!object->findChild<QObject *>("closeButton"));
    QVERIFY(!object->findChild<QObject *>("roundedMask"));
    QTRY_COMPARE(shell->x(), 0.0);
    QTRY_COMPARE(shell->y(), 0.0);
    QTRY_COMPARE(shell->size(), QSizeF(980, 840));
}

void QmlLoadTest::migrationViewLoadsInNativeHost()
{
    MockController controller;
    PreviewDownloads downloads;
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    view.rootContext()->setContextProperty(QStringLiteral("previewDownloads"), &downloads);
    view.setInitialProperties({{QStringLiteral("migrationView"), true}});
    view.setSource(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.resize(980, 840);
    view.show();
    QTRY_VERIFY(view.rootObject()->property("migrationReady").toBool());
    auto *loader = view.rootObject()->findChild<QObject *>(QStringLiteral("migrationLoader"));
    QVERIFY(loader);
    auto *preview = loader->property("item").value<QQuickItem *>();
    QVERIFY(preview);
    QCOMPARE(preview->objectName(), QStringLiteral("downloadsPreview"));
    QVERIFY(preview->findChild<QObject *>(QStringLiteral("previewDownloadList")));
    QVERIFY(!preview->property("shellMargin").isValid());
    QVERIFY(!preview->property("shellRadius").isValid());
    QCOMPARE(preview->x(), 0.0); QCOMPARE(preview->y(), 0.0);
}

void QmlLoadTest::backdropQmlLoads()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/Backdrop.qml")));
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("objectName").toString(), QStringLiteral("backdropRoot"));
    QCOMPARE(object->property("pattern").toString(), QStringLiteral("stripes"));
    QCOMPARE(object->property("color").value<QColor>(), QColor(14, 14, 20, 255));

    const auto colorForPattern = [&object](const char *pattern) {
        object->setProperty("pattern", QString::fromLatin1(pattern));
        return object->property("color").value<QColor>();
    };
    QCOMPARE(colorForPattern("black"), QColor(0, 0, 0, 255));
    QCOMPARE(colorForPattern("white"), QColor(255, 255, 255, 255));
    QCOMPARE(colorForPattern("magenta"), QColor(255, 0, 255, 255));
    QCOMPARE(colorForPattern("stripes"), QColor(14, 14, 20, 255));
}

void QmlLoadTest::optionalKirigamiExampleCompiles()
{
    const QString importRoot = QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
    if (!QFileInfo(importRoot + QStringLiteral("/org/kde/kirigami/qmldir")).isFile()) {
        QSKIP("org.kde.kirigami QML module is not installed");
    }

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/KirigamiDialogProbe.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
}

QTEST_MAIN(QmlLoadTest)
#include "tst_qmlload.moc"
