#pragma once

#include <QRegion>

class QWindow;

namespace capability {

bool kf6BlurBridgeBuilt();
bool blurEffectReportedAvailable();
bool requestBlurBehind(QWindow *window, bool enabled, const QRegion &region);

} // namespace capability
