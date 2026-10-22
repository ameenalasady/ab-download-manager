#include "regiongeometry.h"

#include <QtTest>

class RegionGeometryTest : public QObject
{
    Q_OBJECT

private slots:
    void roundedRegionHasExactBounds();
    void roundedRegionExcludesCorners();
    void oversizedRadiusIsClamped();
    void shadowMarginIsExcludedFromBlurAndInput();
    void maximizedUsesSquaredFullWindow();
    void inputAndBlurShareOneSilhouette();
    void emptyAndTinySizesAreSafe();
    void zeroMarginStillUsesCorners();
};

void RegionGeometryTest::roundedRegionHasExactBounds()
{
    const QRect rect(16, 16, 868, 728);
    const QRegion region = makeRoundedRegion(rect, 18.0);

    QCOMPARE(region.boundingRect(), rect);
    QVERIFY(!region.isEmpty());
    QVERIFY(region.rectCount() > 1);
    QVERIFY(region.contains(rect.center()));
}

void RegionGeometryTest::roundedRegionExcludesCorners()
{
    const QRect rect(16, 16, 868, 728);
    const QRegion region = makeRoundedRegion(rect, 18.0);

    QVERIFY(!region.contains(rect.topLeft()));
    QVERIFY(!region.contains(QPoint(rect.right(), rect.top())));
    QVERIFY(!region.contains(QPoint(rect.left(), rect.bottom())));
    QVERIFY(!region.contains(rect.bottomRight()));
    QVERIFY(region.contains(QPoint(rect.left() + 20, rect.top() + 20)));
}

void RegionGeometryTest::oversizedRadiusIsClamped()
{
    const QRect rect(0, 0, 40, 40);
    const QRegion region = makeRoundedRegion(rect, 500.0);

    QCOMPARE(region.boundingRect(), rect);
    QVERIFY(!region.contains(rect.topLeft()));
    QVERIFY(region.contains(rect.center()));
}

void RegionGeometryTest::shadowMarginIsExcludedFromBlurAndInput()
{
    const QSize size(900, 760);
    const CapabilityRegions regions = computeCapabilityRegions(size, false, 16, 18.0);

    QCOMPARE(regions.contentRect, QRect(16, 16, 868, 728));
    QCOMPARE(regions.inputRegion.boundingRect(), regions.contentRect);
    QCOMPARE(regions.blurRegion.boundingRect(), regions.contentRect);
    QVERIFY(!regions.inputRegion.contains(QPoint(0, 0)));
    QVERIFY(!regions.blurRegion.contains(QPoint(size.width() - 1, size.height() - 1)));
    QVERIFY(regions.blurRegion.contains(regions.contentRect.center()));
}

void RegionGeometryTest::maximizedUsesSquaredFullWindow()
{
    const QSize size(1280, 720);
    const CapabilityRegions regions = computeCapabilityRegions(size, true, 16, 18.0);

    QCOMPARE(regions.contentRect, QRect(0, 0, 1280, 720));
    QCOMPARE(regions.cornerRadius, 0.0);
    QCOMPARE(regions.inputRegion, QRegion(QRect(0, 0, 1280, 720)));
    QCOMPARE(regions.blurRegion, QRegion(QRect(0, 0, 1280, 720)));
    QCOMPARE(regions.inputRegion.rectCount(), 1);
}

void RegionGeometryTest::inputAndBlurShareOneSilhouette()
{
    const CapabilityRegions regions = computeCapabilityRegions(QSize(777, 555), false, 16, 18.0);

    QCOMPARE(regions.inputRegion, regions.blurRegion);
}

void RegionGeometryTest::emptyAndTinySizesAreSafe()
{
    CapabilityRegions empty = computeCapabilityRegions(QSize(), false, 16, 18.0);
    QVERIFY(empty.contentRect.isEmpty());
    QVERIFY(empty.inputRegion.isEmpty());
    QVERIFY(empty.blurRegion.isEmpty());

    CapabilityRegions tooSmall = computeCapabilityRegions(QSize(20, 10), false, 16, 18.0);
    QVERIFY(tooSmall.contentRect.isEmpty());
    QVERIFY(tooSmall.inputRegion.isEmpty());
    QVERIFY(tooSmall.blurRegion.isEmpty());

    CapabilityRegions noMargin = computeCapabilityRegions(QSize(100, 100), false, 0, 10.0);
    QCOMPARE(noMargin.contentRect, QRect(0, 0, 100, 100));
    QCOMPARE(noMargin.inputRegion.boundingRect(), QRect(0, 0, 100, 100));
}

void RegionGeometryTest::zeroMarginStillUsesCorners()
{
    const CapabilityRegions regions = computeCapabilityRegions(QSize(100, 100), false, 0, 10.0);

    QVERIFY(!regions.inputRegion.contains(QPoint(0, 0)));
    QVERIFY(regions.inputRegion.contains(QPoint(50, 50)));
    QVERIFY(regions.inputRegion.rectCount() > 1);
}

QTEST_GUILESS_MAIN(RegionGeometryTest)
#include "tst_regiongeometry.moc"
