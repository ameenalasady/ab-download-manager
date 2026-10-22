#include "kwindowsystembridge.h"

#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
#include <KWindowEffects>
#endif

namespace capability {

bool kf6BlurBridgeBuilt()
{
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
    return true;
#else
    return false;
#endif
}

bool blurEffectReportedAvailable()
{
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
    return KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
#else
    return false;
#endif
}

bool requestBlurBehind(QWindow *window, bool enabled, const QRegion &region)
{
#ifdef QT_CAPABILITY_HAS_KF6_WINDOWSYSTEM
    if (!window) {
        return false;
    }
    // This is a request only. The compositor decides whether to blur, and this
    // shell must never report the request as proof that blur was rendered.
    KWindowEffects::enableBlurBehind(window, enabled, enabled ? region : QRegion());
    return true;
#else
    Q_UNUSED(window);
    Q_UNUSED(enabled);
    Q_UNUSED(region);
    return false;
#endif
}

} // namespace capability
