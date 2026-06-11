#include <geolocation/provider.h>

#include <allocator/allocator.h>
#include <debug/assert.h>
#include <log/log.h>
#include <time/nano.h>

#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <objidl.h>
#include <windows.foundation.h>
#include <windows.devices.geolocation.h>
#include <windows.devices.geolocation.geofencing.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator                          Geo_ILocator;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocatorStatics                  Geo_ILocatorStatics;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIGeoposition                         Geo_IPosition;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate                       Geo_ICoordinate;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIPositionChangedEventArgs            Geo_IPositionArgs;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIStatusChangedEventArgs              Geo_IStatusArgs;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CPositionStatus                       Geo_PositionStatus;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CGeolocationAccessStatus              Geo_AccessStatus;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CBasicGeoposition                     Geo_BasicPosition;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIGeocircleFactory                    Geo_ICircleFactory;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIGeocircle                           Geo_ICircle;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CIGeoshape                            Geo_IShape;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitor         Geo_IFenceMonitor;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitorStatics  Geo_IFenceMonitorStatics;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofence                Geo_IFence;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceFactory         Geo_IFenceFactory;
typedef __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceStateChangeReport Geo_IFenceReport;

typedef __FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CPositionChangedEventArgs Geo_PositionHandler;
typedef __FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CStatusChangedEventArgs   Geo_StatusHandler;
typedef __FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceMonitor_IInspectable                             Geo_FenceHandler;
typedef __FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeoposition          Geo_PositionCompleted;
typedef __FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus Geo_AccessCompleted;
typedef __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeoposition                          Geo_PositionOp;
typedef __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus              Geo_AccessOp;
typedef __FIVector_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofence                        Geo_FenceVector;
typedef __FIVectorView_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceStateChangeReport   Geo_FenceReportView;
typedef __FIReference_1_double                                                                      Geo_RefDouble;

static const Mel_Geo_Provider_Sink* g_sink;
static Geo_ILocator*                g_locator;
static Geo_IFenceMonitor*           g_fence_monitor;
static Geo_FenceVector*             g_fences;
static EventRegistrationToken       g_position_token;
static EventRegistrationToken       g_status_token;
static EventRegistrationToken       g_fence_token;
static bool                         g_streaming;
static Mel_Future*                  g_auth_future;
static Mel_Geo_Request*             g_pending;
static SRWLOCK                      g_last_lock = SRWLOCK_INIT;
static Mel_Geo_Fix                  g_last_fix;
static bool                         g_have_last;
static LONG                         g_access_state;

#define GEO_WIN_ACCESS_UNKNOWN 0
#define GEO_WIN_ACCESS_ALLOWED 1
#define GEO_WIN_ACCESS_DENIED  2

static HSTRING geo_win__hstr(const WCHAR* s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, (UINT32)wcslen(s), &h);
    return h;
}

static f64 geo_win__ref_double(Geo_RefDouble* ref, bool* has)
{
    *has = false;
    if (ref == NULL)
        return 0.0;
    DOUBLE v = 0.0;
    if (SUCCEEDED(__FIReference_1_double_get_Value(ref, &v)))
        *has = true;
    __FIReference_1_double_Release(ref);
    return v;
}

static Mel_Geo_Fix geo_win__fix_from_position(Geo_IPosition* pos)
{
    Mel_Geo_Fix fix = { 0 };
    Geo_ICoordinate* coord = NULL;
    if (FAILED(__x_ABI_CWindows_CDevices_CGeolocation_CIGeoposition_get_Coordinate(pos, &coord)) || coord == NULL)
        return fix;

    DOUBLE lat = 0.0, lon = 0.0, acc = 0.0;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_Latitude(coord, &lat);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_Longitude(coord, &lon);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_Accuracy(coord, &acc);
    fix.latitude_deg = lat;
    fix.longitude_deg = lon;
    fix.horizontal_accuracy_m = acc;
    fix.valid |= MEL_GEO_VALID_POSITION | MEL_GEO_VALID_HACC;

    Geo_RefDouble* ref = NULL;
    bool           has = false;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_Altitude(coord, &ref);
    f64 alt = geo_win__ref_double(ref, &has);
    if (has)
    {
        fix.altitude_m = alt;
        fix.valid |= MEL_GEO_VALID_ALTITUDE;
    }
    ref = NULL;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_AltitudeAccuracy(coord, &ref);
    f64 vacc = geo_win__ref_double(ref, &has);
    if (has)
    {
        fix.vertical_accuracy_m = vacc;
        fix.valid |= MEL_GEO_VALID_VACC;
    }
    ref = NULL;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_Speed(coord, &ref);
    f64 speed = geo_win__ref_double(ref, &has);
    if (has)
    {
        fix.speed_mps = speed;
        fix.valid |= MEL_GEO_VALID_SPEED;
    }
    ref = NULL;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_Heading(coord, &ref);
    f64 heading = geo_win__ref_double(ref, &has);
    if (has)
    {
        fix.course_deg = heading;
        fix.valid |= MEL_GEO_VALID_COURSE;
    }

    __x_ABI_CWindows_CFoundation_CDateTime ts = { 0 };
    if (SUCCEEDED(__x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_get_Timestamp(coord, &ts)))
    {
        fix.utc_unix_ms = (u64)((ts.UniversalTime - 116444736000000000ll) / 10000ll);
        fix.valid |= MEL_GEO_VALID_UTC;
    }
    fix.monotonic_ns = mel_nanos_since_unspecified_epoch();
    fix.valid |= MEL_GEO_VALID_MONOTONIC;

    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocoordinate_Release(coord);
    return fix;
}

static void geo_win__remember(const Mel_Geo_Fix* fix)
{
    AcquireSRWLockExclusive(&g_last_lock);
    g_last_fix = *fix;
    g_have_last = true;
    ReleaseSRWLockExclusive(&g_last_lock);
}

static void geo_win__satisfy_pending(const Mel_Geo_Fix* fix, Geo_PositionOp* op, const mel_geo_result* r)
{
    Mel_Geo_Request** pp = &g_pending;
    while (*pp != NULL)
    {
        Mel_Geo_Request* req = *pp;
        if ((void*)op == NULL || req->provider_data == (void*)op)
        {
            *pp = req->provider_next;
            req->provider_next = NULL;
            req->provider_data = NULL;
            g_sink->on_request(req, fix, r);
            if (op != NULL)
                return;
        }
        else
            pp = &req->provider_next;
    }
}

static HRESULT STDMETHODCALLTYPE geo_win__qi_position(Geo_PositionHandler* self, REFIID riid, void** out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAgileObject) ||
        IsEqualIID(riid, &IID___FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CPositionChangedEventArgs))
    {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE geo_win__addref(void* self)
{
    (void)self;
    return 2;
}

static ULONG STDMETHODCALLTYPE geo_win__release(void* self)
{
    (void)self;
    return 1;
}

static HRESULT STDMETHODCALLTYPE geo_win__on_position(Geo_PositionHandler* self, Geo_ILocator* sender, Geo_IPositionArgs* args)
{
    (void)self;
    (void)sender;
    Geo_IPosition* pos = NULL;
    if (FAILED(__x_ABI_CWindows_CDevices_CGeolocation_CIPositionChangedEventArgs_get_Position(args, &pos)) || pos == NULL)
        return S_OK;
    Mel_Geo_Fix fix = geo_win__fix_from_position(pos);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeoposition_Release(pos);
    if (fix.valid & MEL_GEO_VALID_POSITION)
    {
        geo_win__remember(&fix);
        g_sink->on_fix(&fix);
    }
    return S_OK;
}

static __FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CPositionChangedEventArgsVtbl g_position_vtbl = {
    .QueryInterface = geo_win__qi_position,
    .AddRef = (void*)geo_win__addref,
    .Release = (void*)geo_win__release,
    .Invoke = geo_win__on_position,
};
static Geo_PositionHandler g_position_handler = { &g_position_vtbl };

static HRESULT STDMETHODCALLTYPE geo_win__qi_status(Geo_StatusHandler* self, REFIID riid, void** out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAgileObject) ||
        IsEqualIID(riid, &IID___FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CStatusChangedEventArgs))
    {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE geo_win__on_status(Geo_StatusHandler* self, Geo_ILocator* sender, Geo_IStatusArgs* args)
{
    (void)self;
    (void)sender;
    Geo_PositionStatus st = PositionStatus_Ready;
    if (FAILED(__x_ABI_CWindows_CDevices_CGeolocation_CIStatusChangedEventArgs_get_Status(args, &st)))
        return S_OK;
    if (st == PositionStatus_Ready)
        g_sink->on_stream_result(&mel_geo_ok);
    else if (st == PositionStatus_Disabled)
        g_sink->on_stream_result(&mel_geo_denied);
    else if (st == PositionStatus_NoData || st == PositionStatus_NotAvailable)
        g_sink->on_stream_result(&mel_geo_unavailable);
    return S_OK;
}

static __FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CStatusChangedEventArgsVtbl g_status_vtbl = {
    .QueryInterface = geo_win__qi_status,
    .AddRef = (void*)geo_win__addref,
    .Release = (void*)geo_win__release,
    .Invoke = geo_win__on_status,
};
static Geo_StatusHandler g_status_handler = { &g_status_vtbl };

static HRESULT STDMETHODCALLTYPE geo_win__qi_position_done(Geo_PositionCompleted* self, REFIID riid, void** out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAgileObject) ||
        IsEqualIID(riid, &IID___FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeoposition))
    {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE geo_win__on_position_done(Geo_PositionCompleted* self, Geo_PositionOp* op, AsyncStatus status)
{
    (void)self;
    if (status != Completed)
    {
        geo_win__satisfy_pending(NULL, op, &mel_geo_unavailable);
        return S_OK;
    }
    Geo_IPosition* pos = NULL;
    if (FAILED(__FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeoposition_GetResults(op, &pos)) || pos == NULL)
    {
        geo_win__satisfy_pending(NULL, op, &mel_geo_unavailable);
        return S_OK;
    }
    Mel_Geo_Fix fix = geo_win__fix_from_position(pos);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeoposition_Release(pos);
    geo_win__remember(&fix);
    geo_win__satisfy_pending(&fix, op, &mel_geo_ok);
    return S_OK;
}

static __FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeopositionVtbl g_position_done_vtbl = {
    .QueryInterface = geo_win__qi_position_done,
    .AddRef = (void*)geo_win__addref,
    .Release = (void*)geo_win__release,
    .Invoke = geo_win__on_position_done,
};
static Geo_PositionCompleted g_position_done_handler = { &g_position_done_vtbl };

static HRESULT STDMETHODCALLTYPE geo_win__qi_access_done(Geo_AccessCompleted* self, REFIID riid, void** out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAgileObject) ||
        IsEqualIID(riid, &IID___FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus))
    {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE geo_win__on_access_done(Geo_AccessCompleted* self, Geo_AccessOp* op, AsyncStatus status)
{
    (void)self;
    Geo_AccessStatus access = GeolocationAccessStatus_Unspecified;
    if (status == Completed)
        __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus_GetResults(op, &access);
    const mel_geo_auth* auth = &mel_geo_auth_not_determined;
    if (access == GeolocationAccessStatus_Allowed)
    {
        InterlockedExchange(&g_access_state, GEO_WIN_ACCESS_ALLOWED);
        auth = &mel_geo_auth_granted_in_use;
    }
    else if (access == GeolocationAccessStatus_Denied)
    {
        InterlockedExchange(&g_access_state, GEO_WIN_ACCESS_DENIED);
        auth = &mel_geo_auth_denied;
    }
    Mel_Future* f = g_auth_future;
    g_auth_future = NULL;
    g_sink->on_auth(f, auth);
    return S_OK;
}

static __FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatusVtbl g_access_done_vtbl = {
    .QueryInterface = geo_win__qi_access_done,
    .AddRef = (void*)geo_win__addref,
    .Release = (void*)geo_win__release,
    .Invoke = geo_win__on_access_done,
};
static Geo_AccessCompleted g_access_done_handler = { &g_access_done_vtbl };

static Mel_Geo_Region* geo_win__region_from_id(HSTRING id)
{
    UINT32       len = 0;
    const WCHAR* buf = WindowsGetStringRawBuffer(id, &len);
    if (buf == NULL || len < 8)
        return NULL;
    unsigned long long ptr = wcstoull(buf + 7, NULL, 16);
    return (Mel_Geo_Region*)(uintptr_t)ptr;
}

static HRESULT STDMETHODCALLTYPE geo_win__qi_fence(Geo_FenceHandler* self, REFIID riid, void** out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IAgileObject) ||
        IsEqualIID(riid, &IID___FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceMonitor_IInspectable))
    {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE geo_win__on_fence(Geo_FenceHandler* self, Geo_IFenceMonitor* sender, IInspectable* args)
{
    (void)self;
    (void)args;
    Geo_FenceReportView* reports = NULL;
    if (FAILED(__x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitor_ReadReports(sender, &reports)) || reports == NULL)
        return S_OK;
    UINT32 n = 0;
    __FIVectorView_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceStateChangeReport_get_Size(reports, &n);
    for (UINT32 i = 0; i < n; i++)
    {
        Geo_IFenceReport* report = NULL;
        if (FAILED(__FIVectorView_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceStateChangeReport_GetAt(reports, i, &report)) || report == NULL)
            continue;
        __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CGeofenceState state;
        Geo_IFence* fence = NULL;
        if (SUCCEEDED(__x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceStateChangeReport_get_NewState(report, &state)) &&
            SUCCEEDED(__x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceStateChangeReport_get_Geofence(report, &fence)) &&
            fence != NULL)
        {
            HSTRING id = NULL;
            if (SUCCEEDED(__x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofence_get_Id(fence, &id)))
            {
                Mel_Geo_Region* region = geo_win__region_from_id(id);
                if (region != NULL)
                {
                    if (state == GeofenceState_Entered)
                        g_sink->on_region(region, true);
                    else if (state == GeofenceState_Exited)
                        g_sink->on_region(region, false);
                }
                WindowsDeleteString(id);
            }
            __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofence_Release(fence);
        }
        __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceStateChangeReport_Release(report);
    }
    __FIVectorView_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceStateChangeReport_Release(reports);
    return S_OK;
}

static __FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceMonitor_IInspectableVtbl g_fence_vtbl = {
    .QueryInterface = geo_win__qi_fence,
    .AddRef = (void*)geo_win__addref,
    .Release = (void*)geo_win__release,
    .Invoke = geo_win__on_fence,
};
static Geo_FenceHandler g_fence_handler = { &g_fence_vtbl };

static bool geo_win__make_locator(void)
{
    if (g_locator != NULL)
        return true;
    HSTRING cls = geo_win__hstr(RuntimeClass_Windows_Devices_Geolocation_Geolocator);
    IInspectable* insp = NULL;
    HRESULT hr = RoActivateInstance(cls, &insp);
    WindowsDeleteString(cls);
    if (FAILED(hr) || insp == NULL)
        return false;
    hr = IInspectable_QueryInterface(insp, &IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator, (void**)&g_locator);
    IInspectable_Release(insp);
    return SUCCEEDED(hr) && g_locator != NULL;
}

static bool geo_win__make_fence_monitor(void)
{
    if (g_fence_monitor != NULL)
        return true;
    HSTRING cls = geo_win__hstr(RuntimeClass_Windows_Devices_Geolocation_Geofencing_GeofenceMonitor);
    Geo_IFenceMonitorStatics* statics = NULL;
    HRESULT hr = RoGetActivationFactory(cls, &IID___x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitorStatics, (void**)&statics);
    WindowsDeleteString(cls);
    if (FAILED(hr) || statics == NULL)
        return false;
    hr = __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitorStatics_get_Current(statics, &g_fence_monitor);
    __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitorStatics_Release(statics);
    if (FAILED(hr) || g_fence_monitor == NULL)
        return false;
    if (FAILED(__x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitor_get_Geofences(g_fence_monitor, &g_fences)))
    {
        __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitor_Release(g_fence_monitor);
        g_fence_monitor = NULL;
        return false;
    }
    __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitor_add_GeofenceStateChanged(g_fence_monitor, &g_fence_handler, &g_fence_token);
    return true;
}

static bool geo_win_available(void* user)
{
    (void)user;
    RoInitialize(RO_INIT_MULTITHREADED);
    return geo_win__make_locator();
}

static void geo_win_attach(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink)
{
    (void)user;
    (void)vat;
    g_sink = sink;
    geo_win__make_locator();
}

static void geo_win_detach(void* user)
{
    (void)user;
    if (g_locator != NULL)
    {
        if (g_streaming)
        {
            __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_remove_PositionChanged(g_locator, g_position_token);
            __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_remove_StatusChanged(g_locator, g_status_token);
        }
        __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_Release(g_locator);
        g_locator = NULL;
    }
    if (g_fence_monitor != NULL)
    {
        __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitor_remove_GeofenceStateChanged(g_fence_monitor, g_fence_token);
        __FIVector_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofence_Release(g_fences);
        __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitor_Release(g_fence_monitor);
        g_fence_monitor = NULL;
        g_fences = NULL;
    }
    g_streaming = false;
    g_sink = NULL;
}

static Mel_Geo_Caps geo_win_caps(void* user)
{
    (void)user;
    return (Mel_Geo_Caps){
        .fixes = true,
        .regions_native = geo_win__make_fence_monitor(),
    };
}

static const mel_geo_auth* geo_win_authorization(void* user)
{
    (void)user;
    LONG st = InterlockedCompareExchange(&g_access_state, 0, 0);
    if (st == GEO_WIN_ACCESS_ALLOWED)
        return &mel_geo_auth_granted_in_use;
    if (st == GEO_WIN_ACCESS_DENIED)
        return &mel_geo_auth_denied;
    return &mel_geo_auth_not_determined;
}

static void geo_win_authorize(void* user, const mel_geo_scope* scope, Mel_Future* future)
{
    (void)user;
    (void)scope;
    HSTRING cls = geo_win__hstr(RuntimeClass_Windows_Devices_Geolocation_Geolocator);
    Geo_ILocatorStatics* statics = NULL;
    HRESULT hr = RoGetActivationFactory(cls, &IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeolocatorStatics, (void**)&statics);
    WindowsDeleteString(cls);
    if (FAILED(hr) || statics == NULL)
    {
        g_sink->on_auth(future, &mel_geo_auth_restricted);
        return;
    }
    Geo_AccessOp* op = NULL;
    hr = __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocatorStatics_RequestAccessAsync(statics, &op);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocatorStatics_Release(statics);
    if (FAILED(hr) || op == NULL)
    {
        g_sink->on_auth(future, &mel_geo_auth_restricted);
        return;
    }
    mel_assert_msg("an authorize is already pending", g_auth_future == NULL);
    g_auth_future = future;
    __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus_put_Completed(op, &g_access_done_handler);
    __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus_Release(op);
}

static const mel_geo_result* geo_win_last_known(void* user, Mel_Geo_Fix* out)
{
    (void)user;
    AcquireSRWLockShared(&g_last_lock);
    bool have = g_have_last;
    if (have)
        *out = g_last_fix;
    ReleaseSRWLockShared(&g_last_lock);
    return have ? &mel_geo_ok : &mel_geo_unavailable;
}

static void geo_win_request(void* user, Mel_Geo_Request* req)
{
    (void)user;
    Geo_PositionOp* op = NULL;
    if (FAILED(__x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_GetGeopositionAsync(g_locator, &op)) || op == NULL)
    {
        g_sink->on_request(req, NULL, &mel_geo_unavailable);
        return;
    }
    req->provider_data = (void*)op;
    req->provider_next = g_pending;
    g_pending = req;
    __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeoposition_put_Completed(op, &g_position_done_handler);
    __FIAsyncOperation_1_Windows__CDevices__CGeolocation__CGeoposition_Release(op);
}

static void geo_win_request_cancel(void* user, Mel_Geo_Request* req)
{
    (void)user;
    for (Mel_Geo_Request** pp = &g_pending; *pp != NULL; pp = &(*pp)->provider_next)
        if (*pp == req)
        {
            *pp = req->provider_next;
            break;
        }
    req->provider_next = NULL;
    req->provider_data = NULL;
}

static const mel_geo_result* geo_win_stream_start(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    if (!geo_win__make_locator())
        return &mel_geo_unavailable;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_put_DesiredAccuracy(
        g_locator, d->accuracy_m <= 100.0 ? PositionAccuracy_High : PositionAccuracy_Default);
    UINT32 interval_ms = d->min_interval_ns > 0 ? (UINT32)(d->min_interval_ns / 1000000) : 0;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_put_ReportInterval(g_locator, interval_ms);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_put_MovementThreshold(g_locator, d->min_distance_m);
    if (FAILED(__x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_add_PositionChanged(g_locator, &g_position_handler, &g_position_token)))
        return &mel_geo_unavailable;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_add_StatusChanged(g_locator, &g_status_handler, &g_status_token);
    g_streaming = true;
    return &mel_geo_ok;
}

static void geo_win_stream_update(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_put_DesiredAccuracy(
        g_locator, d->accuracy_m <= 100.0 ? PositionAccuracy_High : PositionAccuracy_Default);
    UINT32 interval_ms = d->min_interval_ns > 0 ? (UINT32)(d->min_interval_ns / 1000000) : 0;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_put_ReportInterval(g_locator, interval_ms);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_put_MovementThreshold(g_locator, d->min_distance_m);
}

static void geo_win_stream_stop(void* user)
{
    (void)user;
    if (!g_streaming)
        return;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_remove_PositionChanged(g_locator, g_position_token);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator_remove_StatusChanged(g_locator, g_status_token);
    g_streaming = false;
}

static const mel_geo_result* geo_win_region_add(void* user, Mel_Geo_Region* r)
{
    (void)user;
    if (!geo_win__make_fence_monitor())
        return &mel_geo_unavailable;

    WCHAR idbuf[32];
    swprintf(idbuf, 32, L"melgeo-%llx", (unsigned long long)(uintptr_t)r);
    HSTRING id = geo_win__hstr(idbuf);

    HSTRING            circle_cls = geo_win__hstr(RuntimeClass_Windows_Devices_Geolocation_Geocircle);
    Geo_ICircleFactory* circle_factory = NULL;
    HRESULT hr = RoGetActivationFactory(circle_cls, &IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeocircleFactory, (void**)&circle_factory);
    WindowsDeleteString(circle_cls);
    if (FAILED(hr) || circle_factory == NULL)
    {
        WindowsDeleteString(id);
        return &mel_geo_unavailable;
    }
    Geo_BasicPosition center = { .Latitude = r->latitude_deg, .Longitude = r->longitude_deg };
    Geo_ICircle* circle = NULL;
    hr = __x_ABI_CWindows_CDevices_CGeolocation_CIGeocircleFactory_Create(circle_factory, center, r->radius_m, &circle);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocircleFactory_Release(circle_factory);
    if (FAILED(hr) || circle == NULL)
    {
        WindowsDeleteString(id);
        return &mel_geo_unavailable;
    }
    Geo_IShape* shape = NULL;
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocircle_QueryInterface(circle, &IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeoshape, (void**)&shape);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeocircle_Release(circle);
    if (shape == NULL)
    {
        WindowsDeleteString(id);
        return &mel_geo_unavailable;
    }

    HSTRING           fence_cls = geo_win__hstr(RuntimeClass_Windows_Devices_Geolocation_Geofencing_Geofence);
    Geo_IFenceFactory* fence_factory = NULL;
    hr = RoGetActivationFactory(fence_cls, &IID___x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceFactory, (void**)&fence_factory);
    WindowsDeleteString(fence_cls);
    if (FAILED(hr) || fence_factory == NULL)
    {
        __x_ABI_CWindows_CDevices_CGeolocation_CIGeoshape_Release(shape);
        WindowsDeleteString(id);
        return &mel_geo_unavailable;
    }
    Geo_IFence* fence = NULL;
    hr = __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceFactory_Create(fence_factory, id, shape, &fence);
    __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceFactory_Release(fence_factory);
    __x_ABI_CWindows_CDevices_CGeolocation_CIGeoshape_Release(shape);
    WindowsDeleteString(id);
    if (FAILED(hr) || fence == NULL)
        return &mel_geo_exhausted;

    hr = __FIVector_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofence_Append(g_fences, fence);
    if (FAILED(hr))
    {
        __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofence_Release(fence);
        return &mel_geo_exhausted;
    }
    r->native = (void*)fence;
    return &mel_geo_ok;
}

static void geo_win_region_remove(void* user, Mel_Geo_Region* r)
{
    (void)user;
    Geo_IFence* fence = (Geo_IFence*)r->native;
    r->native = NULL;
    if (fence == NULL || g_fences == NULL)
        return;
    UINT32  index = 0;
    boolean found = 0;
    if (SUCCEEDED(__FIVector_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofence_IndexOf(g_fences, fence, &index, &found)) && found)
        __FIVector_1_Windows__CDevices__CGeolocation__CGeofencing__CGeofence_RemoveAt(g_fences, index);
    __x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofence_Release(fence);
}

void mel_geo__register_host_providers(void)
{
    static Mel_Geo_Provider_Node node;
    node.desc = (Mel_Geo_Provider_Desc){
        .name = "win32-geolocator",
        .available = geo_win_available,
        .attach = geo_win_attach,
        .detach = geo_win_detach,
        .caps = geo_win_caps,
        .authorization = geo_win_authorization,
        .authorize = geo_win_authorize,
        .last_known = geo_win_last_known,
        .request = geo_win_request,
        .request_cancel = geo_win_request_cancel,
        .stream_start = geo_win_stream_start,
        .stream_update = geo_win_stream_update,
        .stream_stop = geo_win_stream_stop,
        .region_add = geo_win_region_add,
        .region_remove = geo_win_region_remove,
    };
    mel_geo_provider_register_host(&node);
}
