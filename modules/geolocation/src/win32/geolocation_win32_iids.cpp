#include <windows.h>
#include <windows.foundation.h>
#include <windows.devices.geolocation.h>
#include <windows.devices.geolocation.geofencing.h>

using ABI::Windows::Foundation::IAsyncOperationCompletedHandler;
using ABI::Windows::Foundation::ITypedEventHandler;
using ABI::Windows::Devices::Geolocation::GeolocationAccessStatus;
using ABI::Windows::Devices::Geolocation::Geolocator;
using ABI::Windows::Devices::Geolocation::Geoposition;
using ABI::Windows::Devices::Geolocation::PositionChangedEventArgs;
using ABI::Windows::Devices::Geolocation::StatusChangedEventArgs;
using ABI::Windows::Devices::Geolocation::Geofencing::GeofenceMonitor;

extern "C" const IID IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeolocator =
    __uuidof(ABI::Windows::Devices::Geolocation::IGeolocator);
extern "C" const IID IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeolocatorStatics =
    __uuidof(ABI::Windows::Devices::Geolocation::IGeolocatorStatics);
extern "C" const IID IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeocircleFactory =
    __uuidof(ABI::Windows::Devices::Geolocation::IGeocircleFactory);
extern "C" const IID IID___x_ABI_CWindows_CDevices_CGeolocation_CIGeoshape =
    __uuidof(ABI::Windows::Devices::Geolocation::IGeoshape);
extern "C" const IID IID___x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceFactory =
    __uuidof(ABI::Windows::Devices::Geolocation::Geofencing::IGeofenceFactory);
extern "C" const IID IID___x_ABI_CWindows_CDevices_CGeolocation_CGeofencing_CIGeofenceMonitorStatics =
    __uuidof(ABI::Windows::Devices::Geolocation::Geofencing::IGeofenceMonitorStatics);

extern "C" const IID IID___FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CPositionChangedEventArgs =
    __uuidof(ITypedEventHandler<Geolocator*, PositionChangedEventArgs*>);
extern "C" const IID IID___FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeolocator_Windows__CDevices__CGeolocation__CStatusChangedEventArgs =
    __uuidof(ITypedEventHandler<Geolocator*, StatusChangedEventArgs*>);
extern "C" const IID IID___FITypedEventHandler_2_Windows__CDevices__CGeolocation__CGeofencing__CGeofenceMonitor_IInspectable =
    __uuidof(ITypedEventHandler<GeofenceMonitor*, IInspectable*>);
extern "C" const IID IID___FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeoposition =
    __uuidof(IAsyncOperationCompletedHandler<Geoposition*>);
extern "C" const IID IID___FIAsyncOperationCompletedHandler_1_Windows__CDevices__CGeolocation__CGeolocationAccessStatus =
    __uuidof(IAsyncOperationCompletedHandler<GeolocationAccessStatus>);
