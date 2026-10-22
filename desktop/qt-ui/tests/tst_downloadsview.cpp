#include "previewdownloads.h"
#include "nativewindowstyle.h"
#include <QAccessible>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QtTest>
#include <cmath>

class DownloadsViewTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        QGuiApplication::setDesktopFileName(QStringLiteral("org.abdownloadmanager.QtCapability.ViewTest-%1").arg(QCoreApplication::applicationPid()));
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }
    void loadsAndResizes();
    void addDialogValidationAndCancel();
    void removeConfirmationUsesStableId();
    void actionsMenuOpens();
    void keyboardAndSearch();
    void multipleSelectionGestures();
    void workflowDialogsKeepTheirFirstExposedSize();
    void buttonsKeepKeyboardFocusWithoutNativeTextRectangle();
    void desktopThemeAndReviewGallery();
};

static void loadPreview(QQuickView &view, PreviewDownloads &model)
{
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setInitialProperties({{"downloads", QVariant::fromValue(&model)}});
    view.setSource(QUrl("qrc:/qt-ui/qml/DownloadsView.qml"));
    view.resize(948, 762);
    view.show();
}

static QObject *child(QQuickView &view, const char *name)
{
    return view.rootObject()->findChild<QObject *>(QString::fromLatin1(name));
}

static QQuickWindow *dialogWindow(QQuickView &view, const char *name)
{
    const auto *dialog = child(view, name);
    auto *content = dialog ? qvariant_cast<QQuickItem *>(dialog->property("contentItem")) : nullptr;
    return content ? content->window() : nullptr;
}

void DownloadsViewTest::loadsAndResizes()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QCOMPARE(view.status(), QQuickView::Ready);
    QVERIFY(view.rootObject());
    QCOMPARE(child(view, "aboutPreviewDialog")->property("title").toString(), "About Download Manager");
    for (const char *name : {"settingsToolbarButton", "showDownloadDetails", "editDownloadOptions", "copyDownloadLink", "addMultipleDownloads"})
        QVERIFY2(!child(view, name)->property("visible").toBool(), name);
    for (auto *object : view.rootObject()->findChildren<QObject *>()) {
        QVERIFY(object->property("text").toString() != "Main");
        QVERIFY(!object->property("text").toString().contains("Remove Example"));
    }
    for (const QSize &size : {QSize(948, 762), QSize(728, 522), QSize(1280, 900)}) {
        view.resize(size);
        QTRY_COMPARE(view.rootObject()->size(), QSizeF(size));
        for (const char *name : {"addExampleButton", "pauseExampleButton", "resumeExampleButton",
                                "exampleActionsButton", "previewSearch", "downloadTableViewport"}) {
            auto *item = qobject_cast<QQuickItem *>(child(view, name));
            QVERIFY(item);
            QTRY_VERIFY(item->width() > 0 && item->height() > 0);
            const QRectF bounds = item->mapRectToScene(QRectF(QPointF(), item->size()));
            QVERIFY2(QRectF(QPointF(), size).contains(bounds), name);
            for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem())
                QCOMPARE(ancestor->opacity(), 1.0);
        }
    }
    auto *list = qobject_cast<QQuickItem *>(child(view, "previewDownloadList"));
    auto *viewport = qobject_cast<QQuickItem *>(child(view, "downloadTableViewport"));
    QVERIFY(list && viewport);
    // Table columns may scroll horizontally at narrow widths; the clipping viewport
    // stays in-window and the fixed header never occludes the first row.
    QVERIFY(viewport->clip());
    QVERIFY(list->mapToItem(viewport, QPointF()).y() >= 34);
    QCOMPARE(list->property("spacing").toInt(), 0);
    auto *add = child(view, "addExampleButton");
    auto *accessible = QAccessible::queryAccessibleInterface(add);
    QVERIFY(accessible);
    QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
    QVERIFY(!child(view, "pauseExampleButton")->property("enabled").toBool());
    model.select("preview-1");
    QTRY_VERIFY(child(view, "pauseExampleButton")->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(child(view, "pauseExampleButton"), "clicked"));
    QVERIFY(model.canResume());
}

void DownloadsViewTest::addDialogValidationAndCancel()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QCOMPARE(view.status(), QQuickView::Ready);
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
    QTRY_VERIFY(view.rootObject()->property("addDialogVisible").toBool());
    const QString capturePath = qEnvironmentVariable("QT_UI_TEST_ADD_FRAME");
    if (!capturePath.isEmpty()) {
        QTest::qWait(100);
        auto *window = dialogWindow(view, "addExampleDialog");
        QVERIFY(window);
        QVERIFY(window->grabWindow().save(capturePath)); // Owned dialog only, never desktop content.
    }
    auto *url = child(view, "exampleUrlInput");
    auto *name = child(view, "exampleNameInput");
    QVERIFY(url && name);
    url->setProperty("text", "file:///not-allowed");
    QVERIFY(QMetaObject::invokeMethod(child(view, "confirmAddExample"), "clicked"));
    QCOMPARE(model.rowCount(), 5);
    QVERIFY(!model.errorMessage().isEmpty());
    QVERIFY(view.rootObject()->property("addDialogVisible").toBool());
    url->setProperty("text", "https://example.invalid/test");
    name->setProperty("text", "new-example.zip");
    QVERIFY(QMetaObject::invokeMethod(child(view, "confirmAddExample"), "clicked"));
    QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.selectedName(), "new-example.zip");
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
    QVERIFY(model.errorMessage().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(child(view, "addExampleDialog"), "reject"));
    QCOMPARE(model.rowCount(), 6);
}

void DownloadsViewTest::removeConfirmationUsesStableId()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QCOMPARE(view.status(), QQuickView::Ready);
    model.select("preview-2");
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
    QTRY_VERIFY(view.rootObject()->property("removeDialogVisible").toBool());
    model.select("preview-1");
    QVERIFY(!child(view, "confirmRemoveExample")->property("enabled").toBool());
    // Even a programmatic signal cannot remove a different selected ID.
    QVERIFY(QMetaObject::invokeMethod(child(view, "confirmRemoveExample"), "clicked"));
    QCOMPARE(model.rowCount(), 5);
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
    QVERIFY(QMetaObject::invokeMethod(child(view, "confirmRemoveExample"), "clicked"));
    QCOMPARE(model.rowCount(), 4);
    QVERIFY(model.selectedId().isEmpty());
}

void DownloadsViewTest::actionsMenuOpens()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QCOMPARE(view.status(), QQuickView::Ready);
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openActionsMenu"));
    QTRY_VERIFY(view.rootObject()->property("actionsMenuVisible").toBool());
    QVERIFY(QMetaObject::invokeMethod(child(view, "exampleActionsMenu"), "close"));
    QTRY_VERIFY(!view.rootObject()->property("actionsMenuVisible").toBool());
}

void DownloadsViewTest::keyboardAndSearch()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QCOMPARE(view.status(), QQuickView::Ready);
    view.requestActivate();
    QTRY_VERIFY(view.isActive());
    auto *list = qobject_cast<QQuickItem *>(child(view, "previewDownloadList"));
    QVERIFY(list);
    list->forceActiveFocus();
    QTest::keyClick(&view, Qt::Key_Down);
    QTRY_VERIFY(!model.selectedId().isEmpty());
    QVERIFY(model.canPause());
    QTest::keyClick(&view, Qt::Key_Space);
    QTRY_VERIFY(model.canResume());
    QTest::keyClick(&view, Qt::Key_F, Qt::ControlModifier);
    auto *search = qobject_cast<QQuickItem *>(child(view, "previewSearch"));
    QTRY_VERIFY(search->hasActiveFocus());
    for (char key : QByteArray("plasma")) QTest::keyClick(&view, key);
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.search(), "plasma");
    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
    for (char key : QByteArray("no results")) QTest::keyClick(&view, key);
    QTRY_COMPARE(model.rowCount(), 0);
    QVERIFY(model.selectedId().isEmpty());
    QTest::keyClick(&view, Qt::Key_N, Qt::ControlModifier);
    QTRY_VERIFY(view.rootObject()->property("addDialogVisible").toBool());
    auto *popup = dialogWindow(view, "addExampleDialog");
    QVERIFY(popup && popup != &view);
    QTest::keyClick(popup, Qt::Key_Escape);
    QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
    QCOMPARE(model.counts().value("all").toInt(), 5);
}

void DownloadsViewTest::multipleSelectionGestures()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.requestActivate();
    QTRY_VERIFY(view.isActive());
    auto *list = qobject_cast<QQuickItem *>(child(view, "previewDownloadList"));
    QVERIFY(list);
    const qreal rowHeight = list->property("contentItem").value<QQuickItem *>()->childItems().first()->height();
    const auto point = [&](int row) { return list->mapToScene(QPointF(60, rowHeight * (row + 0.5))).toPoint(); };
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, point(0));
    QCOMPARE(model.selectedIds(), QStringList{"preview-1"});
    QTest::mouseClick(&view, Qt::LeftButton, Qt::ControlModifier, point(2));
    QCOMPARE(model.selectionCount(), 2);
    QTest::mouseClick(&view, Qt::LeftButton, Qt::ShiftModifier, point(4));
    QCOMPARE(model.selectedIds(), (QStringList{"preview-3", "preview-4", "preview-5"}));
    // Right-click inside a multiple selection must not discard the other rows.
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, point(3));
    QCOMPARE(model.selectionCount(), 3);
    QVERIFY(QMetaObject::invokeMethod(child(view, "exampleActionsMenu"), "close"));
    list->forceActiveFocus();
    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(model.selectionCount(), 5);
    QTest::keyClick(&view, Qt::Key_Escape);
    QCOMPARE(model.selectionCount(), 0);
    QTest::keyClick(&view, Qt::Key_Down);
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(model.selectedIds(), (QStringList{"preview-1", "preview-2"}));
}

// Observe natural native geometry; unlike gallery capture helpers this never
// locks or writes a window size. Ignore the hidden parent and observe each popup
// from its first compositor exposure through late configure delivery.
class FirstPopupExposure : public QObject
{
public:
    QQuickWindow *parentWindow = nullptr;
    QPointer<QQuickWindow> popup;
    QSize firstSize;
    QList<QSize> exposedResizes;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto *window = qobject_cast<QQuickWindow *>(object);
        if (event->type() == QEvent::Expose && window && window != parentWindow
            && window->isVisible() && firstSize.isEmpty()) {
            popup = window;
            firstSize = window->size();
        }
        if (event->type() == QEvent::Resize && window && window == popup && window->isVisible())
            exposedResizes.append(window->size());
        return false;
    }
};

void DownloadsViewTest::workflowDialogsKeepTheirFirstExposedSize()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    model.select("preview-2");
    FirstPopupExposure observer;
    observer.parentWindow = &view;
    qApp->installEventFilter(&observer);
    for (const auto *name : {"aboutPreviewDialog", "addExampleDialog", "removeExampleDialog"}) {
        observer.firstSize = {};
        observer.popup = nullptr;
        observer.exposedResizes.clear();
        if (QByteArray(name) == "addExampleDialog")
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
        else if (QByteArray(name) == "removeExampleDialog")
            QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
        else
            QVERIFY(QMetaObject::invokeMethod(child(view, name), "open"));
        auto *window = dialogWindow(view, name);
        QVERIFY(window && window != &view);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *material = child(view, name)->findChild<NativeWindowStyle *>();
        QVERIFY(material);
        QTRY_COMPARE(material->window(), window);
        QVERIFY(window->format().alphaBufferSize() >= 8);
        QTRY_VERIFY(!observer.firstSize.isEmpty());
        QCOMPARE(observer.popup.data(), window);
        QTest::qWait(1800);
        qInfo() << "natural workflow first/settled" << name << observer.firstSize << window->size();
        QCOMPARE(window->size(), observer.firstSize);
        for (const auto &size : std::as_const(observer.exposedResizes))
            QCOMPARE(size, observer.firstSize);
        QVERIFY(!window->grabWindow().isNull());
        QVERIFY(QMetaObject::invokeMethod(child(view, name), "close"));
        QTRY_VERIFY(!window->isVisible());
    }
    qApp->removeEventFilter(&observer);
}

void DownloadsViewTest::buttonsKeepKeyboardFocusWithoutNativeTextRectangle()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
    auto *window = dialogWindow(view, "addExampleDialog");
    auto *button = qobject_cast<QQuickItem *>(child(view, "confirmAddExample"));
    QVERIFY(window && button);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_VERIFY(button->property("highlighted").toBool());
    auto *outline = button->findChild<QQuickItem *>("buttonKeyboardFocusFrame");
    QVERIFY(outline);
    // The desktop StyleItem used activeFocus OR highlighted as its paint hint, painting
    // a label box. A primary (highlighted, here in preview mode) button has its own accent
    // frame with no native focus paint; any native frame must keep the hint off.
    const auto noNativeFocusPaint = [&] {
        auto *current = qvariant_cast<QQuickItem *>(button->property("background"));
        return current && (current->objectName() == QLatin1String("buttonPrimaryFrame")
            || current->property("hasFocus") == QVariant(false));
    };
    // Emphasis never replaces the deferred native delegate; toggling it cannot
    // leave a hidden or dangling background on popup/default changes.
    auto *native = qvariant_cast<QQuickItem *>(button->property("background"));
    auto *primary = button->findChild<QQuickItem *>("buttonPrimaryFrame"); QVERIFY(primary);
    button->setProperty("highlighted", true);
    QTRY_VERIFY(primary->isVisible());
    QCOMPARE(qvariant_cast<QQuickItem *>(button->property("background")), native);
    QVERIFY(noNativeFocusPaint());
    button->setProperty("highlighted", false);
    QTRY_VERIFY(!primary->isVisible());
    QCOMPARE(qvariant_cast<QQuickItem *>(button->property("background")), native);
    QTRY_VERIFY(native->isVisible());
    button->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(button->hasActiveFocus());
    QTRY_VERIFY(button->property("visualFocus").toBool());
    QTRY_VERIFY(outline->isVisible());
    QCOMPARE(outline->size(), button->size());
    QVERIFY(noNativeFocusPaint());
    button->setProperty("highlighted", true);
    QTRY_VERIFY(primary->isVisible());
    QCOMPARE(outline->size(), button->size());
    auto *accessible = QAccessible::queryAccessibleInterface(button);
    QVERIFY(accessible && !accessible->text(QAccessible::Name).isEmpty());
    QVERIFY(!accessible->state().disabled);
    const int focusHold = qBound(0, qEnvironmentVariableIntValue("QT_FOCUS_CHROME_HOLD_MS"), 10000);
    if (focusHold) QTest::qWait(focusHold);
    // Keep the standard AbstractButton keyboard activation and normal focus chain.
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(model.rowCount(), 6);
    QTRY_VERIFY(!view.rootObject()->property("addDialogVisible").toBool());
}

static double luminance(const QColor &color)
{
    const auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

void DownloadsViewTest::desktopThemeAndReviewGallery()
{
    PreviewDownloads model;
    QQuickView view;
    loadPreview(view, model);
    QCOMPARE(view.status(), QQuickView::Ready);
    auto *theme = child(view, "desktopTheme");
    QVERIFY(theme);
    // A hidden Kirigami token provider previously resolved all colors to black.
    // Verify actual resolved colors, not hard-coded Layan or QApplication tokens.
    for (const char *role : {"surface", "row", "alternate", "selected", "hover", "field"}) {
        const auto background = theme->property(role).value<QColor>();
        QVERIFY(background.isValid()); QCOMPARE(background.alpha(), 255);
        for (const char *foreground : {"text", "secondary", "error", "positive", "warning", "focusColor"}) {
            const auto text = theme->property(foreground).value<QColor>();
            QVERIFY(text.isValid()); QCOMPARE(text.alpha(), 255);
            const auto a = luminance(text), b = luminance(background);
            const auto minimum = QByteArray(foreground) == "focusColor" ? 3.0 : 4.5;
            const auto message = QByteArray(foreground) + " on " + role;
            QVERIFY2((qMax(a, b) + 0.05) / (qMin(a, b) + 0.05) >= minimum, message.constData());
        }
    }
    const QString directory = qEnvironmentVariable("QT_UI_REVIEW_DIR");
    // Lock logical geometry while capturing: a Wayland configure can otherwise
    // supersede resize() and silently label a narrow frame as the default size.
    QSize frame(948, 762);
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto lock = [&](const QSize &size) {
        frame = size;
        view.setMaximumSize(QSize(16777215, 16777215));
        view.setMinimumSize(size); view.setMaximumSize(size); view.resize(size);
        // A late compositor configure can still change the size; require it to stay put.
        QElapsedTimer stable;
        return QTest::qWaitFor([&] {
            if (view.size() != size || view.rootObject()->size() != QSizeF(size)) {
                if (view.size() != size) view.resize(size);
                stable.invalidate(); return false;
            }
            if (!stable.isValid()) stable.start();
            return stable.elapsed() >= 400;
        }, 8000);
    };
    const auto capture = [&](const QString &name) {
        // KWin may re-apply remembered geometry for this app id after the first
        // configure; re-assert the requested frame instead of mislabelling it.
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (view.size() != frame && !lock(frame)) return false;
            QTest::qWait(350); // Let style/Kirigami padding animations settle.
            if (view.size() == frame) break;
        }
        const auto image = view.grabWindow();
        const QSize pixels(qRound(frame.width() * view.devicePixelRatio()), qRound(frame.height() * view.devicePixelRatio()));
        // Fractional scale factors may round the buffer by one device pixel.
        const bool sized = view.size() == frame && qAbs(image.width() - pixels.width()) <= 1 && qAbs(image.height() - pixels.height()) <= 1;
        if (!sized) qWarning() << name << view.size() << image.size() << pixels;
        return !image.isNull() && sized && (directory.isEmpty() || image.save(directory + "/" + name + ".png"));
    };
    QVERIFY(lock(QSize(948, 762)));
    qInfo() << "review platform" << QGuiApplication::platformName() << "dpr" << view.devicePixelRatio();
    // Default width: every optional column fits without horizontal scrolling.
    auto *viewport = qobject_cast<QQuickItem *>(child(view, "downloadTableViewport"));
    QVERIFY(viewport);
    QTRY_VERIFY(viewport->property("showDate").toBool());
    QCOMPARE(viewport->property("contentWidth").toReal(), viewport->width());
    // Workflow dialogs share native transient chrome rather than inconsistent scrims.
    QVERIFY(!child(view, "aboutPreviewDialog")->property("shellMargin").isValid());
    QVERIFY(capture("home"));
    model.select("preview-2");
    qreal previousMenuX = -1;
    for (const QString &name : {QString("File"), QString("Help")}) {
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openMenu", Q_ARG(QString, name)));
        const char *object = name == "File" ? "fileMenu" : name == "Tasks" ? "tasksMenu" : name == "Tools" ? "toolsMenu" : "helpMenu";
        // Each menu opens below its own menubar button, left to right.
        const qreal x = child(view, object)->property("x").toReal();
        QVERIFY2(x > previousMenuX, qPrintable(name)); previousMenuX = x;
        QVERIFY(capture("menu-" + name.toLower()));
        QVERIFY(QMetaObject::invokeMethod(child(view, object), "close"));
    }
    const auto captureDialog = [&](const char *object, const QString &name) {
        auto *window = dialogWindow(view, object);
        if (!window || window == &view || !QTest::qWaitForWindowExposed(window)) return false;
        QTest::qWait(350);
        const auto image = window->grabWindow();
        // Optional owned-window compositor inspection (includes server-side decorations).
        // Normal CTest runs never wait here.
        const int hold = qBound(0, qEnvironmentVariableIntValue("QT_UI_CHROME_HOLD_MS"), 10000);
        if (hold) QTest::qWait(hold);
        return !image.isNull() && (directory.isEmpty() || image.save(directory + "/" + name + ".png"));
    };
    QVERIFY(QMetaObject::invokeMethod(child(view, "aboutPreviewDialog"), "open"));
    QVERIFY(captureDialog("aboutPreviewDialog", "about"));
    QVERIFY(QMetaObject::invokeMethod(child(view, "aboutPreviewDialog"), "close"));
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openAddDialog"));
    QVERIFY(captureDialog("addExampleDialog", "add"));
    QVERIFY(QMetaObject::invokeMethod(child(view, "addExampleDialog"), "reject"));
    QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "openRemoveDialog"));
    QVERIFY(captureDialog("removeExampleDialog", "remove"));
    QVERIFY(QMetaObject::invokeMethod(child(view, "removeExampleDialog"), "reject"));
    QVERIFY(lock(QSize(728, 522)));
    QTRY_VERIFY(!viewport->property("showDate").toBool());
    QVERIFY(viewport->property("contentWidth").toReal() <= viewport->width());
    QVERIFY(capture("home-small"));
    model.setSearch("no matching download");
    auto *empty = qobject_cast<QQuickItem *>(child(view, "downloadsEmptyState"));
    QVERIFY(empty);
    QTRY_VERIFY(empty->isVisible());
    // The empty state stays inside the visible table viewport, not the scrolled content.
    const QRectF emptyBounds = empty->mapRectToItem(viewport, QRectF(QPointF(), empty->size()));
    QVERIFY(QRectF(QPointF(), viewport->size()).contains(emptyBounds));
    QVERIFY(capture("empty-small"));
}

QTEST_MAIN(DownloadsViewTest)
#include "tst_downloadsview.moc"
