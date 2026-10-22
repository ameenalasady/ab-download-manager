#include "previewdownloads.h"
#include "desktopopener.h"
#include <QAbstractItemModelTester>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

namespace {
QString stateOf(PreviewDownloads &model, const QString &id)
{
    return model.data(model.index(model.indexOf(id)), PreviewDownloads::TransferState).toString();
}
}

class PreviewDownloadsTest : public QObject
{
    Q_OBJECT
private slots:
    void fixtureRolesAndCounts();
    void filterSearchAndStableSelection();
    void pauseResumeAndRemove();
    void addValidatesAndRevealsExample();
    void invalidExamples_data();
    void invalidExamples();
    void multiSelectionToggleAndRanges();
    void selectionPrunesHiddenRowsAndStaysQuiet();
    void bulkActionsTouchOnlyEligibleRows();
    void desktopOpenerOnlyAcceptsRealLocalEntries();
};

void PreviewDownloadsTest::fixtureRolesAndCounts()
{
    PreviewDownloads model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(model.rowCount(model.index(0)), 0);
    QCOMPARE(model.counts().value("active").toInt(), 2);
    QCOMPARE(model.counts().value("completed").toInt(), 1);
    QVERIFY(!model.data({}, Qt::DisplayRole).isValid());
    QSet<QString> ids;
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto index = model.index(i);
        ids.insert(model.data(index, PreviewDownloads::StableId).toString());
        QVERIFY(!model.data(index, PreviewDownloads::FileName).toString().isEmpty());
        const double progress = model.data(index, PreviewDownloads::Progress).toDouble();
        QVERIFY(progress >= 0 && progress <= 1);
    }
    QCOMPARE(ids.size(), 5);
    QCOMPARE(model.idAt(0), "preview-1");
    QVERIFY(model.idAt(-1).isEmpty());
    QVERIFY(model.idAt(500).isEmpty());
    QVERIFY(model.selectedId().isEmpty());
    QVERIFY(!model.canPause());
    QVERIFY(!model.canResume());
}

void PreviewDownloadsTest::filterSearchAndStableSelection()
{
    PreviewDownloads model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.select("preview-1");
    model.setCategory("active");
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.selectedId(), "preview-1");
    model.setSearch("PLASMA");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.selectedId(), "preview-1");
    model.setSearch("does not exist");
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.selectedId().isEmpty());
    model.select("preview-2"); // hidden rows are not actionable
    QVERIFY(model.selectedId().isEmpty());
    model.setCategory("invalid");
    QCOMPARE(model.category(), "active");
    model.setSearch("");
    model.setCategory("all");
    QCOMPARE(model.rowCount(), 5);
    model.select("missing");
    QVERIFY(model.selectedId().isEmpty());
}

void PreviewDownloadsTest::pauseResumeAndRemove()
{
    PreviewDownloads model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(!model.removeSelected());
    model.select("preview-1");
    QVERIFY(model.canPause());
    QVERIFY(model.pauseSelected());
    QCOMPARE(model.selectedId(), "preview-1");
    QCOMPARE(model.counts().value("paused").toInt(), 2);
    QVERIFY(!model.pauseSelected());
    QVERIFY(model.canResume());
    QVERIFY(model.resumeSelected());
    QCOMPARE(model.data(model.index(0), PreviewDownloads::TransferState).toString(), "queued");
    model.select("preview-4");
    QVERIFY(!model.canPause());
    QVERIFY(!model.canResume());
    QVERIFY(!model.resumeSelected());
    QVERIFY(model.removeSelected());
    QCOMPARE(model.rowCount(), 4);
    QVERIFY(model.selectedId().isEmpty());
    model.setCategory("failed");
    model.select("preview-5");
    QVERIFY(model.resumeSelected());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.selectedId().isEmpty());
}

void PreviewDownloadsTest::addValidatesAndRevealsExample()
{
    PreviewDownloads model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setCategory("completed");
    model.setSearch("missing");
    QVERIFY(model.addExample("https://example.invalid/test.zip", " Test.zip "));
    QCOMPARE(model.category(), "all");
    QVERIFY(model.search().isEmpty());
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.selectedId(), "preview-6");
    QCOMPARE(model.selectedName(), "Test.zip");
    QCOMPARE(model.data(model.index(0), PreviewDownloads::TransferState).toString(), "queued");
    QVERIFY(model.removeSelected());
    QVERIFY(model.addExample("http://example.invalid/next", "next"));
    QCOMPARE(model.selectedId(), "preview-7"); // IDs never reused after removal
    QVERIFY(model.errorMessage().isEmpty());
}

void PreviewDownloadsTest::invalidExamples_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("name");
    QTest::newRow("empty-url") << "" << "ok.zip";
    QTest::newRow("file-url") << "file:///etc/passwd" << "ok.zip";
    QTest::newRow("credentials") << "https://user:secret@example.invalid/a" << "ok.zip";
    QTest::newRow("malformed") << "https://[bad" << "ok.zip";
    QTest::newRow("empty-name") << "https://example.invalid/a" << " ";
    QTest::newRow("path") << "https://example.invalid/a" << "../a";
    QTest::newRow("windows-path") << "https://example.invalid/a" << "a\\b";
    QTest::newRow("dot") << "https://example.invalid/a" << "..";
    QTest::newRow("control") << "https://example.invalid/a" << "a\nb";
    QTest::newRow("long-name") << "https://example.invalid/a" << QString(181, 'a');
    QTest::newRow("long-url") << ("https://example.invalid/" + QString(2048, 'a')) << "ok.zip";
}

void PreviewDownloadsTest::invalidExamples()
{
    QFETCH(QString, url);
    QFETCH(QString, name);
    PreviewDownloads model;
    QVERIFY(!model.addExample(url, name));
    QCOMPARE(model.rowCount(), 5);
    QVERIFY(!model.errorMessage().isEmpty());
    model.clearError();
    QVERIFY(model.errorMessage().isEmpty());
}

void PreviewDownloadsTest::multiSelectionToggleAndRanges()
{
    PreviewDownloads model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    // Ctrl+click adds and removes single rows; the last click is current and anchor.
    model.toggleSelected("preview-3");
    QCOMPARE(model.selectedIds(), QStringList{"preview-3"});
    QCOMPARE(model.selectedId(), "preview-3");
    QCOMPARE(model.selectionCount(), 1);
    model.toggleSelected("preview-1");
    QCOMPARE(model.selectedIds(), (QStringList{"preview-1", "preview-3"})); // Always visible order.
    QCOMPARE(model.selectedId(), "preview-1");
    QVERIFY(model.isSelected("preview-3"));
    model.toggleSelected("preview-1");
    QCOMPARE(model.selectedIds(), QStringList{"preview-3"});
    QCOMPARE(model.data(model.index(0), PreviewDownloads::Selected).toBool(), false);
    QCOMPARE(model.data(model.index(2), PreviewDownloads::Selected).toBool(), true);
    // Shift range from the anchor replaces the selection and keeps the anchor for growing it.
    model.select("preview-2");
    QCOMPARE(model.selectedName(), "Design-reference.pdf");
    model.selectRange("preview-4");
    QCOMPARE(model.selectedIds(), (QStringList{"preview-2", "preview-3", "preview-4"}));
    QCOMPARE(model.selectedId(), "preview-4");
    model.selectRange("preview-5");
    QCOMPARE(model.selectedIds(), (QStringList{"preview-2", "preview-3", "preview-4", "preview-5"}));
    model.selectRange("preview-1"); // Shrinking works from the same anchor too.
    QCOMPARE(model.selectedIds(), (QStringList{"preview-1", "preview-2"}));
    // Shift+Ctrl extends from the anchor instead of replacing the rest.
    model.select("preview-1");
    model.toggleSelected("preview-5");
    QCOMPARE(model.selectedIds(), (QStringList{"preview-1", "preview-5"}));
    model.selectRange("preview-2", true);
    QCOMPARE(model.selectedIds(), (QStringList{"preview-1", "preview-2", "preview-3", "preview-4", "preview-5"}));
    // Hidden and unknown rows are never actionable.
    model.setCategory("completed");
    model.selectAll();
    QCOMPARE(model.selectedIds(), QStringList{"preview-4"});
    const auto before = model.selectedIds();
    model.toggleSelected("preview-1");
    model.selectRange("preview-2", true);
    model.select("preview-5");
    model.select("preview-unknown");
    QCOMPARE(model.selectedIds(), before);
    QVERIFY(!model.isSelected("preview-5"));
    model.setCategory("all");
    QCOMPARE(model.selectedIds(), QStringList{"preview-4"});
}

void PreviewDownloadsTest::selectionPrunesHiddenRowsAndStaysQuiet()
{
    PreviewDownloads model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.selectAll();
    QCOMPARE(model.selectionCount(), 5);
    QCOMPARE(model.selectedIds().size(), 5);
    QCOMPARE(model.selectedId(), "preview-1");
    // Filtering prunes hidden rows for good: clearing the filter does not resurrect them.
    model.setCategory("active");
    QCOMPARE(model.selectedIds(), (QStringList{"preview-1", "preview-3"}));
    model.setCategory("all");
    QCOMPARE(model.selectionCount(), 2);
    QVERIFY(!model.isSelected("preview-2"));
    model.setSearch("wallpaper");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.selectedIds(), QStringList{"preview-3"});
    model.setSearch("nothing matches this");
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.selectedId().isEmpty());
    QVERIFY(!model.canPause());
    QVERIFY(!model.canResume());
    QVERIFY(!model.removeSelected());
    model.selectAll();
    QCOMPARE(model.selectionCount(), 0);
    model.setSearch("");
    QCOMPARE(model.selectionCount(), 0);
    // Idempotent selection calls never notify: a repainted row is not a snapshot change.
    model.select("preview-2");
    QSignalSpy selection(&model, &PreviewDownloads::selectionChanged);
    model.select("preview-2");
    model.toggleSelected("preview-9");
    model.selectRange("preview-9");
    model.selectRange("preview-2");
    QCOMPARE(selection.count(), 0);
    model.selectRange("preview-3");
    QCOMPARE(selection.count(), 1);
    QCOMPARE(model.selectedIds(), (QStringList{"preview-2", "preview-3"}));
    QCOMPARE(model.errorMessage(), QString());
}

void PreviewDownloadsTest::bulkActionsTouchOnlyEligibleRows()
{
    PreviewDownloads model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.select("preview-1");     // downloading
    model.toggleSelected("preview-4"); // completed
    model.toggleSelected("preview-2"); // paused
    QCOMPARE(model.selectionCount(), 3);
    QVERIFY(model.canPause());
    QVERIFY(model.canResume());
    QVERIFY(model.pauseSelected());
    // Only rows that could pause change; paused and completed rows are untouched.
    QCOMPARE(stateOf(model, "preview-1"), "paused");
    QCOMPARE(stateOf(model, "preview-2"), "paused");
    QCOMPARE(stateOf(model, "preview-4"), "completed");
    QCOMPARE(model.counts().value("paused").toInt(), 2);
    QCOMPARE(model.counts().value("active").toInt(), 1);
    QVERIFY(!model.pauseSelected()); // Nothing eligible is left, and a no-op reports false.
    QCOMPARE(model.selectedIds(), (QStringList{"preview-1", "preview-2", "preview-4"}));
    QVERIFY(model.resumeSelected());
    QCOMPARE(stateOf(model, "preview-1"), "queued");
    QCOMPARE(stateOf(model, "preview-2"), "queued");
    QCOMPARE(stateOf(model, "preview-4"), "completed");
    // The untargeted rows keep their own state.
    QCOMPARE(stateOf(model, "preview-3"), "queued");
    QCOMPARE(stateOf(model, "preview-5"), "failed");
    // Multi-row removal deletes exactly the selection and leaves the rest in order.
    model.select("preview-3");
    model.toggleSelected("preview-5");
    QVERIFY(model.removeSelected());
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.selectedId().isEmpty());
    QCOMPARE(model.selectedIds(), QStringList());
    QCOMPARE(model.idAt(0), "preview-1");
    QCOMPARE(model.idAt(1), "preview-2");
    QCOMPARE(model.idAt(2), "preview-4");
    QVERIFY(!model.removeSelected());
    QCOMPARE(model.counts().value("failed").toInt(), 0);
    QCOMPARE(model.counts().value("completed").toInt(), 1);
}

void PreviewDownloadsTest::desktopOpenerOnlyAcceptsRealLocalEntries()
{
    // The opener itself is the safety boundary: anything it refuses never reaches the
    // session D-Bus services or QDesktopServices, so these checks are side-effect free.
    DesktopOpener opener;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath("payload.bin");
    QFile payload(file);
    QVERIFY(payload.open(QIODevice::WriteOnly));
    payload.write("fixture");
    payload.close();
    QVERIFY(!opener.openFile(QUrl()));
    QVERIFY(!opener.openFile(QUrl(QStringLiteral("https://example.invalid/payload.bin"))));
    QVERIFY(!opener.openFile(QUrl::fromLocalFile(directory.filePath("missing.bin"))));
    QVERIFY(!opener.openFile(QUrl::fromLocalFile(QStringLiteral("relative/payload.bin"))));
    QVERIFY(!opener.openFile(QUrl::fromLocalFile(directory.path()))); // A folder is not a file to open.
    QVERIFY(!opener.showInFolder(QUrl()));
    QVERIFY(!opener.showInFolder(QUrl(QStringLiteral("ftp://example.invalid/payload.bin"))));
    QVERIFY(!opener.showInFolder(QUrl::fromLocalFile(directory.filePath("missing.bin"))));
    QVERIFY(!opener.showInFolder(QUrl::fromLocalFile(QStringLiteral("relative"))));
    // Special files and dangling links are neither opened nor revealed.
    const QString pipe = directory.filePath("pipe");
    QCOMPARE(::mkfifo(QFile::encodeName(pipe).constData(), 0600), 0);
    QVERIFY(!opener.openFile(QUrl::fromLocalFile(pipe)));
    QVERIFY(!opener.showInFolder(QUrl::fromLocalFile(pipe)));
    const QString dangling = directory.filePath("dangling");
    QVERIFY(QFile::link(directory.filePath("missing.bin"), dangling));
    QVERIFY(!opener.openFile(QUrl::fromLocalFile(dangling)));
    QVERIFY(!opener.showInFolder(QUrl::fromLocalFile(dangling)));
    // Cleanup must not leave a FIFO behind for the next run.
    QVERIFY(QFile::remove(pipe));
}

QTEST_GUILESS_MAIN(PreviewDownloadsTest)
#include "tst_previewdownloads.moc"
