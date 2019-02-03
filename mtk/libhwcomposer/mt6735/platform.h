#ifndef HWC_PLATFORM_H_
#define HWC_PLATFORM_H_

#include "platform_common.h"

// Device-dependent code should be placed in the Platform. If adding a function into
// Platform, we should also add a condidate into PlatformCommon to avoid build error.
class Platform : public PlatformCommon, public android::Singleton<Platform>
{
public:
    Platform();
    ~Platform() { };

    void initOverlay();
    // isUILayerValid() is ued to verify
    // if ui layer could be handled by hwcomposer
    bool isUILayerValid(int dpy, struct hwc_layer_1* layer,
            PrivateHandle* priv_handle);

    // isMMLayerValid() is used to verify
    // if mm layer could be handled by hwcomposer
    bool isMMLayerValid(int dpy, struct hwc_layer_1* layer,
            PrivateHandle* priv_handle, bool& is_high);

    // getBandwidthLimit() is used to get
    // limitataion of bandwith
    // return -1 if there is no limitation
    int getBandwidthLimit();

    // For 82 ovl hw limitation, pitch of buffers must allign 128 bytes.
    int computePitch(const int& pitch_bytes, const int& width_bytes, int* status);

    size_t getLimitedExternalDisplaySize();

#ifdef USE_HWC2
    bool isUILayerValid(const sp<HWCLayer>& layer, int32_t* line);
    bool isMMLayerValid(const sp<HWCLayer>& layer, int32_t* line);
#endif
};

#endif // HWC_PLATFORM_H_
