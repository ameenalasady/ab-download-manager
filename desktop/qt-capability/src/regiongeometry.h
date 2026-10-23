#pragma once

#include <QRegion>
#include <QRect>
#include <QSize>

/*!
 * The capability shell paints its background only inside a rounded rectangle
 * that is inset from the transparent window edges. The same logical rectangle
 * is used for the input mask and for the compositor blur request.
 */
struct CapabilityRegions {
    QRect contentRect;
    QRegion inputRegion;
    QRegion blurRegion;
    qreal cornerRadius = 0.0;
};

QRegion makeRoundedRegion(const QRect &rect, qreal radius);

CapabilityRegions computeCapabilityRegions(const QSize &logicalSize,
                                           bool maximized,
                                           int shadowMargin,
                                           qreal cornerRadius);
