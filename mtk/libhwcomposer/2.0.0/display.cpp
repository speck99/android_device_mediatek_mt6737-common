#define DEBUG_LOG_TAG "DPY"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <stdint.h>

#include "hwc_priv.h"
#include <cutils/properties.h>

#include <linux/disp_session.h>

#include "utils/debug.h"
#include "utils/tools.h"

#include "dispatcher.h"
#include "display.h"
#include "hwdev.h"
#include "overlay.h"
#include "event.h"
#include "sync.h"
#include "hwc2.h"

// WORKAROUND: boost GPU for MHL on K2
#include "platform.h"
#ifndef MTK_BASIC_PACKAGE
#ifdef USES_POWERHAL
#include <vendor/mediatek/hardware/power/1.1/IPower.h>
#include <vendor/mediatek/hardware/power/1.1/types.h>
using namespace vendor::mediatek::hardware::power::V1_1;
#else
#include "PerfServiceNative.h"
#endif // USE_POWERHAL
#endif // MTK_BASIC_PACKAGE

#include <ged/ged_dvfs.h>
#include <ged/ged.h>

int g_mhl_cpu_scenario = -1;

// ---------------------------------------------------------------------------

ANDROID_SINGLETON_STATIC_INSTANCE(DisplayManager);

sp<UEventThread> g_uevent_thread = NULL;
sp<RefreshRequestThread> g_refresh_thread = NULL;

#ifdef MTK_USER_BUILD
int DisplayManager::m_profile_level = PROFILE_NONE;
#else
int DisplayManager::m_profile_level = PROFILE_COMP | PROFILE_BLT | PROFILE_TRIG;
#endif

DisplayManager::DisplayManager()
    : m_curr_disp_num(0)
    , m_fake_disp_num(0)
    , m_listener(NULL)
    , m_video_hdcp(UINT_MAX)
    , m_ged_log_handle(nullptr)
{
    m_data = (DisplayData*) calloc(1, MAX_DISPLAYS * sizeof(DisplayData));
    LOG_ALWAYS_FATAL_IF(m_data == nullptr, "DisplayManager::m_data calloc(%zu) fail",
        MAX_DISPLAYS * sizeof(DisplayData));

    m_data[HWC_DISPLAY_PRIMARY].trigger_by_vsync = false;
    m_data[HWC_DISPLAY_EXTERNAL].trigger_by_vsync = false;
    m_data[HWC_DISPLAY_VIRTUAL].trigger_by_vsync = false;
    switch (HWCMediator::getInstance().m_features.trigger_by_vsync)
    {
        case 1:
            m_data[HWC_DISPLAY_EXTERNAL].trigger_by_vsync = true;
            break;
        case 2:
            m_data[HWC_DISPLAY_EXTERNAL].trigger_by_vsync = true;
            m_data[HWC_DISPLAY_VIRTUAL].trigger_by_vsync = true;
            break;
    }

    g_uevent_thread = new UEventThread();
    if (g_uevent_thread == NULL)
    {
        HWC_LOGE("Failed to initialize UEvent thread!!");
        abort();
    }
    g_uevent_thread->initialize();

    if (HWCMediator::getInstance().getOvlDevice(HWC_DISPLAY_PRIMARY)->isDispSelfRefreshSupported())
    {
        g_refresh_thread = new RefreshRequestThread();
        if (g_refresh_thread == NULL) {
            HWC_LOGE("Failed to initialize RefreshRequestThread");
            abort();
        }
        g_refresh_thread->initialize();
        g_refresh_thread->setEnabled(true);
    }

    memset(m_state_ultra_display, 0, sizeof(m_state_ultra_display));
    for (int i = 0; i < MAX_DISPLAYS; i++)
    {
        m_display_power_state[i] = false;
    }
}

DisplayManager::~DisplayManager()
{
    m_listener = NULL;
    free(m_data);

    if (m_ged_log_handle != nullptr)
        ged_log_disconnect(m_ged_log_handle);
}

void DisplayManager::init()
{
    m_curr_disp_num = 1;

    if (m_listener != NULL) m_listener->onPlugIn(HWC_DISPLAY_PRIMARY);

    createVsyncThread(HWC_DISPLAY_PRIMARY);

    if (HWCMediator::getInstance().m_features.dual_display)
    {
        checkSecondBuildInPanel();
    }

    HWC_LOGI("Display Information:");
    HWC_LOGI("# fo current devices : %d", m_curr_disp_num);
    for (unsigned int i = 0; i < m_curr_disp_num; i++)
    {
        printDisplayInfo(i);
        m_display_power_state[i] = true;
    }

    m_listener->onHotPlugExt(HWC_DISPLAY_PRIMARY, true);
}

void DisplayManager::resentCallback()
{
    m_listener->onHotPlugExt(HWC_DISPLAY_PRIMARY, true);
}

void DisplayManager::checkSecondBuildInPanel()
{
    hotplugExt(HWC_DISPLAY_EXTERNAL, true, false, false);
}

void DisplayManager::createVsyncThread(int dpy)
{
    AutoMutex _l(m_vsyncs[dpy].lock);
    m_vsyncs[dpy].thread = new VSyncThread(dpy);
    if (m_vsyncs[dpy].thread == NULL)
    {
        HWC_LOGE("dpy=%d/Failed to initialize VSYNC thread!!", dpy);
        abort();
    }
    m_vsyncs[dpy].thread->initialize(!m_data[dpy].has_vsync, m_data[dpy].refresh);
}

void DisplayManager::destroyVsyncThread(int dpy)
{
    if (m_vsyncs[dpy].thread != NULL)
    {
        m_vsyncs[dpy].thread->requestExit();
        m_vsyncs[dpy].thread->setLoopAgain();
        m_vsyncs[dpy].thread->join();
    }

    {
        // We cannot lock the whole destoryVsyncThread(), or it will cause the deadlock between
        // UEventThread and DispatcherThread. When a secondary display plugged out, UEventThread
        // will wait completion of VSyncThread, and needs to acquire the VSync lock of onVSync().
        // DispatcherThread acquired the lock of onVsync() firstly and request a next VSync.
        // Unfortunately, DispatcherThread cannot get a VSync because the vsync lock is acquired
        // by UEventThread.
        AutoMutex _l(m_vsyncs[dpy].lock);
        m_vsyncs[dpy].thread = NULL;
    }
}

void DisplayManager::printDisplayInfo(int dpy)
{
    if (dpy < 0 || dpy >= MAX_DISPLAYS) return;

    DisplayData* disp_data = &m_data[dpy];

    HWC_LOGI("------------------------------------");
    HWC_LOGI("Device id   : %d",   dpy);
    HWC_LOGI("Width       : %d",   disp_data->width);
    HWC_LOGI("Height      : %d",   disp_data->height);
    HWC_LOGI("xdpi        : %f",   disp_data->xdpi);
    HWC_LOGI("ydpi        : %f",   disp_data->ydpi);
    HWC_LOGI("vsync       : %d",   disp_data->has_vsync);
    HWC_LOGI("refresh     : %" PRId64, disp_data->refresh);
    HWC_LOGI("connected   : %d",   disp_data->connected);
    HWC_LOGI("hwrotation  : %d",   disp_data->hwrotation);
    HWC_LOGI("subtype     : %d",   disp_data->subtype);
    HWC_LOGI("aspect      : %1.3f, %1.3f",
        disp_data->aspect_portrait, disp_data->aspect_landscape);
    HWC_LOGI("protrait    : [%4d,%4d,%4d,%4d]",
        disp_data->mir_portrait.left,  disp_data->mir_portrait.top,
        disp_data->mir_portrait.right, disp_data->mir_portrait.bottom);
    HWC_LOGI("landscape   : [%4d,%4d,%4d,%4d]",
        disp_data->mir_landscape.left,  disp_data->mir_landscape.top,
        disp_data->mir_landscape.right, disp_data->mir_landscape.bottom);
    HWC_LOGI("trigger_by_vsync: %d", disp_data->trigger_by_vsync);
    HWC_LOGI("supportS3D  : %d", disp_data->is_s3d_support);
    HWC_LOGI("density     : %d", disp_data->density);

    char value[PROPERTY_VALUE_MAX];
    property_get("ro.sf.lcd_density", value, "0");
    float density = atof(value);
    HWC_LOGI("ro.sf.lcd_density : %d", static_cast<int> (density));
}

// TODO: should check if need to add a lock that protecting against m_curr_disp_num
// to avoid race condition (e.g. when handling the hotplug event)
int DisplayManager::query(int what, int* value)
{
    switch (what)
    {
        case DISP_CURRENT_NUM:
            *value = m_curr_disp_num;
            break;

        default:
            return -EINVAL;
    }

    return 0;
}

void DisplayManager::dump(struct dump_buff* /*log*/)
{
    if (m_vsyncs[HWC_DISPLAY_PRIMARY].thread != NULL)
        m_vsyncs[HWC_DISPLAY_PRIMARY].thread->setProperty();

    if (g_uevent_thread != NULL)
        g_uevent_thread->setProperty();
}

void DisplayManager::setListener(const sp<EventListener>& listener)
{
    m_listener = listener;
}

void DisplayManager::requestVSync(int dpy, bool enabled)
{
    AutoMutex _l(m_vsyncs[dpy].lock);
    if (m_vsyncs[dpy].thread != NULL)
        m_vsyncs[dpy].thread->setEnabled(enabled);
}

void DisplayManager::requestNextVSync(int dpy)
{
    AutoMutex _l(m_vsyncs[dpy].lock);
    if (m_vsyncs[dpy].thread != NULL)
        m_vsyncs[dpy].thread->setLoopAgain();
}

void DisplayManager::vsync(int dpy, nsecs_t timestamp, bool enabled)
{
    if (m_listener != NULL)
    {
        // check if primary display needs to use external vsync source
        if (HWC_DISPLAY_PRIMARY != dpy)
        {
            Mutex::Autolock _l(m_power_lock);

            if (m_data[HWC_DISPLAY_PRIMARY].vsync_source == dpy)
                m_listener->onVSync(HWC_DISPLAY_PRIMARY, timestamp, enabled);
        }

        m_listener->onVSync(dpy, timestamp, enabled);
    }
}

void DisplayManager::hotplugExt(int dpy, bool connected, bool fake, bool notify)
{
    if (dpy != HWC_DISPLAY_EXTERNAL)
    {
        HWC_LOGW("Failed to hotplug external disp(%d) connect(%d) !", dpy, connected);
        return;
    }

    HWC_LOGI("Hotplug external disp(%d) connect(%d) fake(%d)", dpy, connected, fake);

    DisplayData* disp_data = &m_data[HWC_DISPLAY_EXTERNAL];

    disp_data->trigger_by_vsync =
        HWCMediator::getInstance().m_features.trigger_by_vsync > 0 ? true : false;

    if (connected && !disp_data->connected)
    {
        HWCMediator::getInstance().createExternalDisplay();
        if (m_listener != NULL) m_listener->onPlugIn(dpy);

        if (fake == true)
        {
            static int _s_shrink_size = 4;
            _s_shrink_size = (_s_shrink_size == 2) ? 4 : 2;
            memcpy(disp_data, &m_data[0], sizeof(DisplayData));
            disp_data->width   = m_data[0].width / _s_shrink_size;
            disp_data->height  = m_data[0].height / _s_shrink_size;
            disp_data->subtype = FAKE_DISPLAY;

            m_fake_disp_num++;
        }

        createVsyncThread(dpy);

        hotplugPost(dpy, 1, DISP_PLUG_CONNECT, notify);

        if (m_listener != NULL && notify) m_listener->onHotPlugExt(dpy, HWC2_CONNECTION_CONNECTED);

        if (disp_data->trigger_by_vsync)
        {
            requestVSync(dpy, true);
        }
        HWCDispatcher::getInstance().ignoreJob(dpy, false);
    }
    else if (!connected && disp_data->connected)
    {
        HWCDispatcher::getInstance().ignoreJob(dpy, true);

        if (fake == true) m_fake_disp_num--;

        if (disp_data->trigger_by_vsync)
        {
            requestVSync(dpy, false);
        }

        destroyVsyncThread(dpy);

        if (m_listener != NULL)
        {
            m_listener->onPlugOut(dpy);
            if (notify)
            {
                m_listener->onHotPlugExt(dpy, HWC2_CONNECTION_DISCONNECTED);
            }
        }

        HWCMediator::getInstance().destroyExternalDisplay();
    }
}

void DisplayManager::hotplugExtOut()
{
    hotplugPost(HWC_DISPLAY_EXTERNAL, 0, DISP_PLUG_DISCONNECT, true);
}

void DisplayManager::hotplugVir(
    const int32_t& dpy,
    const bool& connected,
    const uint32_t& width,
    const uint32_t& height,
    const int32_t& format)
{
    if (dpy != HWC_DISPLAY_VIRTUAL)
    {
        HWC_LOGW("Failed to hotplug virtual disp(%d) !", dpy);
        return;
    }

    DisplayData* disp_data = &m_data[HWC_DISPLAY_VIRTUAL];

    disp_data->trigger_by_vsync =
        HWCMediator::getInstance().m_features.trigger_by_vsync > 1 ? true : false;

    // todo: should not occurs
    // if (connected == disp_data->connected)
    // {
    //     if (!connected) return;

    //     PrivateHandle priv_handle;
    //     getPrivateHandleInfo(list->outbuf, &priv_handle);

    //     if ((disp_data->width == priv_handle.width) &&
    //         (disp_data->height == priv_handle.height) &&
    //         (disp_data->format == priv_handle.format))
    //     {
    //         return;
    //     }
    //     else
    //     {
    //         HWC_LOGI("Format changed for virtual disp(%d)", dpy);

    //         if (m_listener != NULL) m_listener->onPlugOut(dpy);

    //         hotplugPost(dpy, 0, DISP_PLUG_DISCONNECT);
    //     }
    // }

    HWC_LOGW("Hotplug virtual disp(%d) connect(%d)", dpy, connected);

    if (connected)
    {
        setDisplayDataForVir(HWC_DISPLAY_VIRTUAL, width, height, format);

        hotplugPost(dpy, 1, DISP_PLUG_CONNECT);

        if (m_listener != NULL) m_listener->onPlugIn(dpy);

        // TODO: How HWC receives requests from WFD frameworks
    }
    else
    {
        if (m_listener != NULL) m_listener->onPlugOut(dpy);

        hotplugPost(dpy, 0, DISP_PLUG_DISCONNECT);

        // TODO: How HWC receives requests from WFD frameworks
    }
}

bool DisplayManager::checkIsWfd()
{
    uint32_t ged_status_vector = 0;
    if (m_ged_log_handle == nullptr)
        m_ged_log_handle = ged_log_connect("HWC");

    if (m_ged_log_handle == nullptr)
    {
        HWC_LOGE("get_log_connect(HWC) failed!");
    }
    else
    {
        const uint32_t ged_err = ged_query_info(
                (GED_HANDLE)(m_ged_log_handle),
                GED_EVENT_STATUS,
                sizeof(ged_status_vector),
                &ged_status_vector);

        if (ged_err != GED_OK)
            HWC_LOGE("ged_query_info() fail(%d)", ged_err);
    }

    HWC_LOGI("%s wfd:%d", __func__, ged_status_vector & GED_EVENT_WFD);
    return (ged_status_vector & GED_EVENT_WFD) != 0;
}

void DisplayManager::refresh(int dpy)
{
    if (m_listener != NULL)
    {
        m_listener->onRefresh(dpy);
    }
}

void DisplayManager::refresh(unsigned int type)
{
    if (m_listener != NULL) {
        HWC_LOGI("receive refesh frquest from driver: type[%u]", type);
        if (WAIT_FOR_REFRESH < type && type < REFRESH_TYPE_NUM) {
            m_listener->onRefresh(HWC_DISPLAY_PRIMARY, type);
        }
    }
}

void DisplayManager::setDisplayDataForVir(const int& dpy, const uint32_t& width, const uint32_t& height, const int32_t& format)
{
    DisplayData* disp_data = &m_data[dpy];

    char value[PROPERTY_VALUE_MAX];
    property_get("ro.sf.lcd_density", value, "160");
    float density = atof(value);

    if (dpy == HWC_DISPLAY_VIRTUAL)
    {
        disp_data->width     = width;
        disp_data->height    = height;
        disp_data->format    = format;
        disp_data->xdpi      = m_data[HWC_DISPLAY_PRIMARY].xdpi;
        disp_data->ydpi      = m_data[HWC_DISPLAY_PRIMARY].ydpi;
        disp_data->has_vsync = false;
        disp_data->connected = true;
        disp_data->density   = static_cast<int>(density);

        const bool is_wfd = checkIsWfd();
        if (is_wfd)
        {
            char value[PROPERTY_VALUE_MAX];
            property_get("media.wfd.hdcp", value, "0");
            disp_data->secure  = (atoi(value) == 1);

            property_get("debug.hwc.force_wfd_insecure", value, "-1");
            if (-1 != atoi(value))
            {
                disp_data->secure = atoi(value);
                HWC_LOGI("force set virtual display secure:%d", disp_data->secure);
            }

            disp_data->subtype = HWC_DISPLAY_WIRELESS;
            disp_data->hdcp_version = INT_MAX;
            HWC_LOGI("(%d) hdcp version is %d", dpy, disp_data->hdcp_version);
        }
        else
        {
            disp_data->secure  = false;
            disp_data->subtype = HWC_DISPLAY_MEMORY;
        }

        // [NOTE]
        // only if the display without any physical rotation,
        // same ratio can be applied to both portrait and landscape
        disp_data->aspect_portrait  = float(disp_data->width) / float(disp_data->height);
        disp_data->aspect_landscape = disp_data->aspect_portrait;

        // TODO
        //disp_data->vsync_source = HWC_DISPLAY_VIRTUAL;

        // currently no need for vir disp
        disp_data->hwrotation = 0;

        disp_data->pixels = disp_data->width * disp_data->height;

        disp_data->trigger_by_vsync =
            HWCMediator::getInstance().m_features.trigger_by_vsync > 1 ? true : false;
    }
}

void DisplayManager::setDisplayDataForPhy(int dpy, buffer_handle_t /*outbuf*/)
{
    DisplayData* disp_data = &m_data[dpy];

    char value[PROPERTY_VALUE_MAX];
    property_get("ro.sf.lcd_density", value, "160");
    float density = atof(value);

    if (dpy == HWC_DISPLAY_PRIMARY)
    {
        disp_session_info info;
        HWCMediator::getInstance().getOvlDevice(dpy)->getOverlaySessionInfo(HWC_DISPLAY_PRIMARY, &info);

        disp_data->width     = info.displayWidth;
        disp_data->height    = info.displayHeight;
        disp_data->format    = info.displayFormat;
        unsigned int physicalWidth = 0;
        unsigned int physicalHeight = 0;
        getPhysicalPanelSize(&physicalWidth, &physicalHeight, info);
        disp_data->density   = info.density == 0 ? static_cast<int>(density) : static_cast<int>(info.density);
        disp_data->xdpi      = physicalWidth == 0 ? disp_data->density * 1000:
                                (info.displayWidth * 25.4f * 1000000 / physicalWidth);
        disp_data->ydpi      = physicalHeight == 0 ? disp_data->density * 1000:
                                (info.displayHeight * 25.4f * 1000000 / physicalHeight);
        if (static_cast<int>(info.density) != 0)
        {
            setUpDensity(static_cast<int>(info.density));
        }
        else
        {
            HWC_LOGI("get %d density from drv, density calculated by physical dpi", info.density);
            setUpDensity(static_cast<int>(disp_data->xdpi / 1000));
        }

        disp_data->has_vsync = info.isHwVsyncAvailable;
        disp_data->connected = info.isConnected;

        // TODO: ask from display driver
        disp_data->secure    = true;
        disp_data->hdcp_version = UINT_MAX;
        disp_data->subtype   = HWC_DISPLAY_LCM;

        disp_data->aspect_portrait  = float(info.displayWidth) / float(info.displayHeight);
        disp_data->aspect_landscape = float(info.displayHeight) / float(info.displayWidth);
        disp_data->mir_portrait     = Rect(info.displayWidth, info.displayHeight);
        disp_data->mir_landscape    = Rect(info.displayWidth, info.displayHeight);

        disp_data->vsync_source = HWC_DISPLAY_PRIMARY;

        float refresh_rate = static_cast<float>(info.vsyncFPS) / 100.0;
        if (0 >= refresh_rate) refresh_rate = 60.0;
        disp_data->refresh = nsecs_t(1e9 / refresh_rate);

        // get physically installed rotation for primary display from property
        property_get("ro.sf.hwrotation", value, "0");
        disp_data->hwrotation = atoi(value) / 90;

        disp_data->pixels = disp_data->width * disp_data->height;

        disp_data->trigger_by_vsync = false;
    }
    else if (dpy == HWC_DISPLAY_EXTERNAL)
    {
        disp_session_info info;
        HWCMediator::getInstance().getOvlDevice(dpy)->getOverlaySessionInfo(HWC_DISPLAY_EXTERNAL, &info);
        if (!info.isConnected)
        {
            HWC_LOGE("Failed to add display, hdmi is not connected!");
            return;
        }

        disp_data->width     = info.displayWidth;
        disp_data->height    = info.displayHeight;
        disp_data->format    = info.displayFormat;
        disp_data->density   = info.density == 0 ? static_cast<int>(density) : static_cast<int>(info.density);
        disp_data->xdpi      = info.physicalWidth == 0 ? disp_data->density :
                                (info.displayWidth * 25.4f / info.physicalWidth);
        disp_data->ydpi      = info.physicalHeight == 0 ? disp_data->density :
                                (info.displayHeight * 25.4f / info.physicalHeight);
        disp_data->has_vsync = info.isHwVsyncAvailable;
        disp_data->connected = info.isConnected;
        disp_data->secure    = true;
        disp_data->hdcp_version = info.isHDCPSupported;
        HWC_LOGI("(%d) hdcp version is %d", dpy, disp_data->hdcp_version);
        switch (info.displayType)
        {
            case DISP_IF_HDMI_SMARTBOOK:
                disp_data->subtype = HWC_DISPLAY_SMARTBOOK;
                break;
            case DISP_IF_EPD:
                disp_data->subtype = HWC_DISPLAY_EPAPER;
                break;
            default:
                disp_data->subtype = HWC_DISPLAY_HDMI_MHL;
                break;
        }
        disp_data->is_s3d_support = info.is3DSupport > 0 ? true : false;

        // [NOTE]
        // only if the display without any physical rotation,
        // same ratio can be applied to both portrait and landscape
        disp_data->aspect_portrait  = float(info.displayWidth) / float(info.displayHeight);
        disp_data->aspect_landscape = disp_data->aspect_portrait;

        disp_data->vsync_source = HWC_DISPLAY_EXTERNAL;

        float refresh_rate = static_cast<float>(info.vsyncFPS) / 100.0;
        if (0 >= refresh_rate) refresh_rate = 60.0;
        disp_data->refresh = nsecs_t(1e9 / refresh_rate);

        // get physically installed rotation for extension display from property
        property_get("ro.sf.hwrotation.ext", value, "0");
        disp_data->hwrotation = atoi(value) / 90;

        disp_data->pixels = disp_data->width * disp_data->height;

        disp_data->trigger_by_vsync =
            HWCMediator::getInstance().m_features.trigger_by_vsync > 0 ? true : false;
    }
    else if (dpy == HWC_DISPLAY_VIRTUAL)
    {
        HWC_LOGE("HWC2 should not call setDisplayDataForPhy() for virtual displays");
    }
}

void DisplayManager::setMirrorRegion(int dpy)
{
    DisplayData* main_disp_data = &m_data[HWC_DISPLAY_PRIMARY];
    DisplayData* disp_data = &m_data[dpy];

    // calculate portrait region
    if (main_disp_data->aspect_portrait > disp_data->aspect_portrait)
    {
        // calculate for letterbox
        int portrait_h = disp_data->width / main_disp_data->aspect_portrait;
        int portrait_y = (disp_data->height - portrait_h) / 2;
        disp_data->mir_portrait.left = 0;
        disp_data->mir_portrait.top = portrait_y;
        disp_data->mir_portrait.right = disp_data->width;
        disp_data->mir_portrait.bottom = portrait_y + portrait_h;
    }
    else
    {
        // calculate for pillarbox
    int portrait_w = disp_data->height * main_disp_data->aspect_portrait;
        int portrait_x = (disp_data->width - portrait_w) / 2;
        disp_data->mir_portrait.left = portrait_x;
    disp_data->mir_portrait.top = 0;
        disp_data->mir_portrait.right = portrait_x + portrait_w;
    disp_data->mir_portrait.bottom = disp_data->height;
    }

    // calculate landscape region
    if (main_disp_data->aspect_landscape > disp_data->aspect_landscape)
    {
        // calculate for letterbox
        int landscape_h = disp_data->width / main_disp_data->aspect_landscape;
        int landscape_y = (disp_data->height - landscape_h) / 2;
        disp_data->mir_landscape.left = 0;
        disp_data->mir_landscape.top = landscape_y;
        disp_data->mir_landscape.right = disp_data->width;
        disp_data->mir_landscape.bottom = landscape_y + landscape_h;
    }
    else
    {
        // calculate for pillarbox
        int landscape_w = disp_data->height * main_disp_data->aspect_landscape;
        int landscape_x = (disp_data->width - landscape_w) / 2;
        disp_data->mir_landscape.left = landscape_x;
        disp_data->mir_landscape.top = 0;
        disp_data->mir_landscape.right = landscape_x + landscape_w;
        disp_data->mir_landscape.bottom = disp_data->height;
    }
}

//DCS(for low power-DMA) would use the same EMI port as VENC/DISP/MJC/IPU that
//we need to disable DCS to avoid data loss
static void enableDCS(const bool& enable)
{
    const int32_t fd = open("/sys/power/mtkdcs/mode", O_WRONLY);
    if (fd < 0)
    {
        HWC_LOGE("[Info] fail to open /sys/power/mtkdcs/mode");
    }
    else
    {
        if(enable)
        {
            // adb shell echo exit wfd > /sys/power/mtkcs/mode
            const char *string = "exit wfd";
            write(fd, string, strlen(string));
            HWC_LOGI("Enable DCS (Dynamic channel switch)");
        }
        else
        {
            // adb shell echo venc > /sys/power/mtkdcs/mode
            const char *string = "wfd";
            write(fd, string, strlen(string));
            HWC_LOGI("Disable DCS (Dynamic channel switch)");
        }
        close(fd);
    }
}

void DisplayManager::hotplugPost(int dpy, bool connected, int state, bool print_info)
{
    DisplayData* disp_data = &m_data[dpy];

    switch (state)
    {
        case DISP_PLUG_CONNECT:
            HWC_LOGI("Added Display Information:");
            setMirrorRegion(dpy);
            if (print_info)
            {
                printDisplayInfo(dpy);
            }
            compareDisplaySize(dpy);
            m_curr_disp_num++;

            if (dpy == HWC_DISPLAY_VIRTUAL &&
                Platform::getInstance().m_config.platform == PLATFORM_MT6799)
            {
                enableDCS(false);
            }
#ifndef MTK_BASIC_PACKAGE
#ifdef USES_POWERHAL
            // TODO: should be put in scenario manager
            if (Platform::getInstance().m_config.platform == PLATFORM_MT6735 ||
                Platform::getInstance().m_config.platform == PLATFORM_MT6752 ||
                Platform::getInstance().m_config.platform == PLATFORM_MT6795)
            {
                if (dpy == HWC_DISPLAY_VIRTUAL &&
                    m_data[dpy].subtype != HWC_DISPLAY_WIRELESS)
                    break;
                sp<IPower> powerhal;
                powerhal = IPower::getService();
                if (powerhal == nullptr)
                {
                    HWC_LOGE("cannot find powerhal service!");
                }
                else
                {
                    // reg and enable CPU configuration
                    if (dpy == HWC_DISPLAY_HDMI_MHL)
                    {
                        g_mhl_cpu_scenario = powerhal->scnReg();
                        powerhal->scnConfig(g_mhl_cpu_scenario, MtkPowerCmd::CMD_SET_CLUSTER_CPU_CORE_MIN, 0, 2, 0, 0);
                        powerhal->scnConfig(g_mhl_cpu_scenario, MtkPowerCmd::CMD_SET_CLUSTER_CPU_FREQ_MIN, 0, 819000, 0, 0);
                    }
                    else
                    {
                        g_mhl_cpu_scenario = powerhal->scnReg();
                        powerhal->scnConfig(g_mhl_cpu_scenario, MtkPowerCmd::CMD_SET_CLUSTER_CPU_CORE_MIN, 1, 2, 0, 0);
                    }
                    if (-1 == g_mhl_cpu_scenario)
                    {
                        HWC_LOGW("Failed to register to powerhal service");
                    }
                    else
                    {
                        // enable perf scenario for multi-display session case
                        // need to set auto restore for suspend/resume

                        powerhal->scnEnable(g_mhl_cpu_scenario, (int)MtkHintOp::MTK_HINT_ALWAYS_ENABLE );

                        HWC_LOGI("Change Scenario=%d, dpy=%d connected",
                            g_mhl_cpu_scenario, dpy);
                    }
                }
            }
            else if (Platform::getInstance().m_config.platform == PLATFORM_MT6755 ||
                     Platform::getInstance().m_config.platform == PLATFORM_MT6797 ||
                     Platform::getInstance().m_config.platform == PLATFORM_MT6799)
            {
                if (dpy == HWC_DISPLAY_VIRTUAL &&
                    m_data[dpy].subtype != HWC_DISPLAY_WIRELESS)
                    break;

                sp<IPower> powerhal;
                powerhal = IPower::getService();
                if (powerhal == nullptr)
                {
                    HWC_LOGE("cannot find powerhal service!");
                }
                else
                {
                    // reg and enable CPU configuration
                    // use 2 big core
                    g_mhl_cpu_scenario = powerhal->scnReg();
                    powerhal->scnConfig(g_mhl_cpu_scenario, MtkPowerCmd::CMD_SET_CLUSTER_CPU_CORE_MIN, 1, 2, 0, 0);
                    if (-1 == g_mhl_cpu_scenario)
                    {
                        HWC_LOGW("Failed to register to powerhal service");
                    }
                    else
                    {
                        powerhal->scnEnable(g_mhl_cpu_scenario, (int) MtkHintOp::MTK_HINT_ALWAYS_ENABLE );
                        HWC_LOGI("Change Scenario=%d, dpy=%d connected",
                            g_mhl_cpu_scenario, dpy);
                    }
                }
            }

#else
            // TODO: should be put in scenario manager
            if (Platform::getInstance().m_config.platform == PLATFORM_MT6735 ||
                Platform::getInstance().m_config.platform == PLATFORM_MT6752 ||
                Platform::getInstance().m_config.platform == PLATFORM_MT6795)
            {
                if (dpy == HWC_DISPLAY_VIRTUAL &&
                    m_data[dpy].subtype != HWC_DISPLAY_WIRELESS)
                    break;

                // reg and enable CPU configuration
                if (dpy == HWC_DISPLAY_HDMI_MHL)
                {
                    g_mhl_cpu_scenario = PerfServiceNative_userRegBigLittle(0, 0, 2, 819000);
                }
                else
                {
                    g_mhl_cpu_scenario = PerfServiceNative_userRegBigLittle(2, 0, 0, 0);
                }
                if (-1 == g_mhl_cpu_scenario)
                {
                    HWC_LOGW("Failed to register to PerfService");
                }
                else
                {
                    // enable perf scenario for multi-display session case
                    // need to set auto restore for suspend/resume
                    PerfServiceNative_userRegScnConfig(
                            g_mhl_cpu_scenario,
                            CMD_SET_SCREEN_OFF_STATE,
                            SCREEN_OFF_WAIT_RESTORE,
                            0, 0, 0);
                    PerfServiceNative_userEnable(g_mhl_cpu_scenario);
                    HWC_LOGI("Change Scenario=%d, dpy=%d connected",
                        g_mhl_cpu_scenario, dpy);
                }
            }
            else if (Platform::getInstance().m_config.platform == PLATFORM_MT6755 ||
                     Platform::getInstance().m_config.platform == PLATFORM_MT6797 ||
                     Platform::getInstance().m_config.platform == PLATFORM_MT6799)
            {
                if (dpy == HWC_DISPLAY_VIRTUAL &&
                    m_data[dpy].subtype != HWC_DISPLAY_WIRELESS)
                    break;

                // reg and enable CPU configuration
                // use 2 big core
                g_mhl_cpu_scenario = PerfServiceNative_userRegBigLittle(2, 0, 0, 0);
                if (-1 == g_mhl_cpu_scenario)
                {
                    HWC_LOGW("Failed to register to PerfService");
                }
                else
                {
                    PerfServiceNative_userRegScnConfig(g_mhl_cpu_scenario,
                                                       CMD_SET_SCREEN_OFF_STATE,
                                                       SCREEN_OFF_ENABLE,
                                                       0,
                                                       0,
                                                       0);
                    PerfServiceNative_userEnable(g_mhl_cpu_scenario);
                    HWC_LOGI("Change Scenario=%d, dpy=%d connected",
                        g_mhl_cpu_scenario, dpy);
                }
            }
#endif // USES_POWERHAL
#endif // MTK_BASIC_PACKAGE

            break;

        case DISP_PLUG_DISCONNECT:
            HWC_LOGI("Removed Display Information:");
            if (print_info)
            {
                printDisplayInfo(dpy);
            }
            memset((void*)disp_data, 0, sizeof(DisplayData));
            compareDisplaySize(dpy);
            m_curr_disp_num--;

            if (dpy == HWC_DISPLAY_VIRTUAL &&
                Platform::getInstance().m_config.platform == PLATFORM_MT6799)
            {
                enableDCS(true);
            }
#ifndef MTK_BASIC_PACKAGE
#ifdef USES_POWERHAL
            // TODO: should be put in scenario manager
            // disable and unreg CPU configuration
            if (-1 != g_mhl_cpu_scenario)
            {
                sp<IPower> powerhal;
                powerhal = IPower::getService();
                if (powerhal == nullptr)
                {
                    HWC_LOGE("cannot find powerhal service!");
                }
                else
                {
                    powerhal->scnDisable(g_mhl_cpu_scenario);
                    HWC_LOGI("Change Scenario=%d, dpy=%d disconnected",
                        g_mhl_cpu_scenario, dpy);

                    powerhal->scnUnreg(g_mhl_cpu_scenario);
                    g_mhl_cpu_scenario = -1;
                }
            }
#else
            // TODO: should be put in scenario manager
            if (Platform::getInstance().m_config.platform == PLATFORM_MT6735 ||
                Platform::getInstance().m_config.platform == PLATFORM_MT6752 ||
                Platform::getInstance().m_config.platform == PLATFORM_MT6795)
            {
                // disable and unreg CPU configuration
                if (-1 != g_mhl_cpu_scenario)
                {
                    PerfServiceNative_userDisable(g_mhl_cpu_scenario);
                    HWC_LOGI("Change Scenario=%d, dpy=%d disconnected",
                        g_mhl_cpu_scenario, dpy);

                    PerfServiceNative_userUnreg(g_mhl_cpu_scenario);
                    g_mhl_cpu_scenario = -1;
                }
            }
            else if (Platform::getInstance().m_config.platform == PLATFORM_MT6755 ||
                     Platform::getInstance().m_config.platform == PLATFORM_MT6797 ||
                     Platform::getInstance().m_config.platform == PLATFORM_MT6799)
            {
                // disable and unreg CPU configuration
                if (-1 != g_mhl_cpu_scenario)
                {
                    PerfServiceNative_userDisable(g_mhl_cpu_scenario);
                    HWC_LOGI("Change Scenario=%d, dpy=%d disconnected",
                        g_mhl_cpu_scenario, dpy);

                    PerfServiceNative_userUnregScn(g_mhl_cpu_scenario);
                    PerfServiceNative_userUnreg(g_mhl_cpu_scenario);
                    g_mhl_cpu_scenario = -1;
                }
            }
#endif // USES_POWERHAL
#endif // MTK_BASIC_PACKAGE

            break;

        case DISP_PLUG_NONE:
            HWC_LOGW("Unexpected hotplug: disp(%d:%d) connect(%d)",
                dpy, disp_data->connected, connected);
            return;
    };
}

void DisplayManager::setPowerMode(int dpy, int mode)
{
    Mutex::Autolock _l(m_power_lock);

    if (HWC_DISPLAY_PRIMARY != dpy) return;

    if (m_data[HWC_DISPLAY_EXTERNAL].connected &&
        m_data[HWC_DISPLAY_EXTERNAL].subtype == HWC_DISPLAY_SMARTBOOK)
    {
        m_data[dpy].vsync_source = (HWC2_POWER_MODE_OFF == mode) ?
                                        HWC_DISPLAY_EXTERNAL : HWC_DISPLAY_PRIMARY;
    }
}

void DisplayManager::compareDisplaySize(int dpy)
{
    if (dpy >= MAX_DISPLAYS || dpy < HWC_DISPLAY_PRIMARY) return;

    DisplayData* display_data = &m_data[dpy];
    AutoMutex _l(m_state_lock);
    if (display_data->connected)
    {
        // threshold should be the resolution of primary display
        // however, we only care MHL 4k output now.
        size_t size = display_data->width * display_data->height;
        m_state_ultra_display[dpy] = (size >= Platform::getInstance().getLimitedExternalDisplaySize());
    }
    else
    {
        m_state_ultra_display[dpy] = false;
    }
}

bool DisplayManager::isUltraDisplay(int dpy)
{
    AutoMutex _l(m_state_lock);
    return m_state_ultra_display[dpy];
}

bool DisplayManager::isAllDisplayOff()
{
    AutoMutex _l(m_state_lock);
    int res = 0;
    for (int i = 0; i < MAX_DISPLAYS; i++)
    {
        if (m_display_power_state[i])
        {
            res |= 1 << i;
        }
    }

    HWC_LOGD("all panel state: %x", res);
    return res ? false : true;
}

void DisplayManager::setDisplayPowerState(int dpy, int state)
{
    AutoMutex _l(m_state_lock);

    if (state == HWC2_POWER_MODE_OFF)
    {
        m_display_power_state[dpy] = false;
    }
    else
    {
        m_display_power_state[dpy] = true;
    }
}

int DisplayManager::getDisplayPowerState(const int& dpy)
{
    AutoMutex _l(m_state_lock);
    return m_display_power_state[dpy];
}

void DisplayManager::getPhysicalPanelSize(unsigned int *width, unsigned int *height,
                                          disp_session_info &info)
{
    if (width != NULL)
    {
        if (info.physicalWidthUm != 0)
        {
            *width = info.physicalWidthUm;
        }
        else
        {
            *width = info.physicalWidth * 1000;
        }
    }

    if (height != NULL)
    {
        if (info.physicalHeightUm != 0)
        {
            *height = info.physicalHeightUm;
        }
        else
        {
            *height = info.physicalHeight * 1000;
        }
    }
}

void DisplayManager::setUpDensity(const int& phy_dpi)
{
    // density_table from google cdd
    // NOTICE: density_table should be sorted
    int density_table [] = { 120, 160, 213, 240, 260,
                             280, 300, 320, 340, 360,
                             400, 420, 480, 560, 640};
    int expected_density = 0;
    for (auto& spec_density : density_table)
    {

        if (abs(phy_dpi - spec_density) < abs(phy_dpi - expected_density))
        {
            expected_density = spec_density;
        }
        else
        {
            break;
        }
    }

    char value[16];
    snprintf(value, sizeof(value), "%d", expected_density);
    int err = property_set("ro.sf.lcd_density", value);
    HWC_LOGI("setprop lcd_density %d", expected_density);
    if (err < 0)
        HWC_LOGI("fail to set ro.sf.lcd_density");
}

uint32_t DisplayManager::getVideoHdcp() const
{
    RWLock::AutoRLock _l(m_video_hdcp_rwlock);
    return m_video_hdcp;
}

void DisplayManager::setVideoHdcp(const uint32_t& video_hdcp)
{
    RWLock::AutoWLock _l(m_video_hdcp_rwlock);
    m_video_hdcp = video_hdcp;
}

