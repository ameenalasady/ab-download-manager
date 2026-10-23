#include "regiongeometry.h"

#include <QPainterPath>
#include <QPolygon>

#include <algorithm>

QRegion makeRoundedRegion(const QRect &rect, qreal radius)
{
    if (rect.isEmpty()) {
        return {};
    }

    const qreal limit = std::min(rect.width(), rect.height()) / 2.0;
    const qreal effectiveRadius = std::clamp(radius, 0.0, limit);
    if (effectiveRadius <= 0.0) {
        return QRegion(rect);
    }

    QPainterPath path;
    path.addRoundedRect(QRectF(rect), effectiveRadius, effectiveRadius);
    return QRegion(path.toFillPolygon().toPolygon());
}

CapabilityRegions computeCapabilityRegions(const QSize &logicalSize,
                                           bool maximized,
                                           int shadowMargin,
                                           qreal cornerRadius)
{
    CapabilityRegions regions;

    if (logicalSize.isEmpty()) {
        return regions;
    }

    const int margin = maximized ? 0 : std::max(0, shadowMargin);
    const int width = std::max(0, logicalSize.width() - 2 * margin);
    const int height = std::max(0, logicalSize.height() - 2 * margin);

    regions.contentRect = QRect(QPoint(margin, margin), QSize(width, height));
    regions.cornerRadius = maximized ? 0.0 : cornerRadius;
    regions.inputRegion = makeRoundedRegion(regions.contentRect, regions.cornerRadius);
    regions.blurRegion = regions.inputRegion;
    return regions;
}
