#ifndef HWC_BlitDEV_H_
#define HWC_BlitDEV_H_

#include "hwdev.h"

#include <utils/KeyedVector.h>
#include "DpBlitStream.h"

using namespace android;

class SyncFence;
class DpBlitStream;

struct InvalidParams
{
    InvalidParams()
        : src_ion_fd(-1)
        , dst_ion_fd(-1)
        , src_fence_index(0)
        , dst_fence_index(0)
        , present_fence_index(0)
        , src_fmt(0)
        , dst_fmt(0)
        , src_range(0)
        , dst_range(0)
        , src_is_need_flush(false)
        , dst_is_need_flush(false)
        , src_is_secure(false)
        , dst_is_secure(false)
        , job_sequence(0)
    {
        memset(&src_crop, 0, sizeof(src_crop));
        memset(&dst_crop, 0, sizeof(dst_crop));
    }

    int             src_ion_fd;
    int             dst_ion_fd;
    unsigned int    src_fence_index;
    unsigned int    dst_fence_index;
    unsigned int    present_fence_index;
    unsigned int    src_fmt;
    unsigned int    dst_fmt;
    Rect            src_crop;
    Rect            dst_crop;
    unsigned int    src_range;
    unsigned int    dst_range;
    bool            src_is_need_flush;
    bool            dst_is_need_flush;
    bool            src_is_secure;
    bool            dst_is_secure;
    unsigned int    job_sequence;
};

// --------------------------------------------------------------------------

class BlitDevice : public IOverlayDevice
{
public:
    BlitDevice();
    ~BlitDevice();

    int32_t getType() { return OVL_DEVICE_TYPE_BLITDEV; }

    // initOverlay() initializes overlay related hw setting
    void initOverlay();

    // getDisplayRotation gets LCM's degree
    uint32_t getDisplayRotation(uint32_t dpy);

    // get supported resolutions
    void getSupportedResolution(KeyedVector<int, OvlInSizeArbitrator::InputSize>& config);

    // isDispRszSupported() is used to query if display rsz is supported
    bool isDispRszSupported();

    // isDispRszSupported() is used to query if display rsz is supported
    bool isDispRpoSupported();

    // isPartialUpdateSupported() is used to query if PartialUpdate is supported
    bool isPartialUpdateSupported();

    // isFenceWaitSupported() is used to query if FenceWait is supported
    bool isFenceWaitSupported();

    // isConstantAlphaForRGBASupported() is used to query if PRGBA is supported
    bool isConstantAlphaForRGBASupported();

    // isDispSelfRefreshSupported is used to query if hardware support ioctl of self-refresh
    bool isDispSelfRefreshSupported();

    // getMaxOverlayInputNum() gets overlay supported input amount
    int getMaxOverlayInputNum();

    // createOverlaySession() creates overlay composition session
    status_t createOverlaySession(
        int dpy, DISP_MODE mode = DISP_SESSION_DIRECT_LINK_MODE);

    // destroyOverlaySession() destroys overlay composition session
    void destroyOverlaySession(int dpy);

    // truggerOverlaySession() used to trigger overlay engine to do composition
    status_t triggerOverlaySession(
        int dpy, int present_fence_idx, int ovlp_layer_num,
        int prev_present_fence_fd,
        EXTD_TRIGGER_MODE trigger_mode = TRIGGER_NORMAL);

    // disableOverlaySession() usd to disable overlay session to do composition
    void disableOverlaySession(int dpy,  OverlayPortParam* const* params, int num);

    // setOverlaySessionMode() sets the overlay session mode
    status_t setOverlaySessionMode(int dpy, DISP_MODE mode);

    // getOverlaySessionMode() gets the overlay session mode
    DISP_MODE getOverlaySessionMode(int dpy);

    // getOverlaySessionInfo() gets specific display device information
    status_t getOverlaySessionInfo(int dpy, disp_session_info* info);

    // getAvailableOverlayInput gets available amount of overlay input
    // for different session
    int getAvailableOverlayInput(int dpy);

    // prepareOverlayInput() gets timeline index and fence fd of overlay input layer
    void prepareOverlayInput(int dpy, OverlayPrepareParam* param);

    // updateOverlayInputs() updates multiple overlay input layers
    void updateOverlayInputs(int dpy, OverlayPortParam* const* params, int num, sp<ColorTransform> color_transform);

    // prepareOverlayOutput() gets timeline index and fence fd for overlay output buffer
    void prepareOverlayOutput(int dpy, OverlayPrepareParam* param);

    // disableOverlayOutput() disables overlay output buffer
    void disableOverlayOutput(int dpy);

    // enableOverlayInput() enables single overlay input layer
    void enableOverlayInput(int dpy, OverlayPortParam* param, int id);

    // prepareOverlayPresentFence() gets present timeline index and fence
    void prepareOverlayPresentFence(int dpy, OverlayPrepareParam* param);

    // waitVSync() is used to wait vsync signal for specific display device
    status_t waitVSync(int dpy, nsecs_t *ts);

    // setPowerMode() is used to switch power setting for display
    void setPowerMode(int dpy, int mode);

    // enableOverlayOutput() enables overlay output buffer
    void enableOverlayOutput(int dpy, OverlayPortParam* param);

    disp_caps_info* getCapsInfo();

    // to query valid layers which can handled by OVL
    bool queryValidLayer(disp_layer_info* disp_layer);

    // waitAllJobDone() use to wait driver for processing all job
    status_t waitAllJobDone(const int dpy);

private:

    // m_sync_fence is used to create or wait fence
    sp<SyncFence> m_sync_input_fence;
    sp<SyncFence> m_sync_output_fence;

    // m_blit_stream is a bit blit stream
    DpBlitStream m_blit_stream;

    int m_session_id;
    disp_session_info m_disp_session_info;

    DefaultKeyedVector< int, bool> m_ion_flush_vector;
    mutable Mutex m_vector_lock;

    InvalidParams m_cur_params;

    int m_state;

    disp_caps_info m_caps_info;

    DISP_MODE m_disp_session_state;
    mutable Mutex m_state_lock;

    int32_t m_last_input_ion_fd;
    int32_t m_last_output_ion_fd;
};

// --------------------------------------------------------------------------

#endif // HWC_BlitDEV_H_
