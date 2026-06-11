#include <geolocation/provider.h>

#include <allocator/allocator.h>
#include <debug/assert.h>
#include <log/log.h>
#include <string/str8.h>
#include <time/nano.h>

#include <string.h>

#import <CoreLocation/CoreLocation.h>
#import <Foundation/Foundation.h>

@interface MelGeoDelegate : NSObject <CLLocationManagerDelegate>
@end

static const Mel_Geo_Provider_Sink* g_sink;
static MelGeoDelegate*              g_delegate;
static CLLocationManager*           g_stream_mgr;
static CLLocationManager*           g_oneshot_mgr;
static NSMutableDictionary*         g_region_nodes;
static Mel_Future*                  g_auth_future;
static Mel_Geo_Request*             g_pending;
static bool                         g_streaming;

static CLAuthorizationStatus apple__status(CLLocationManager* mgr)
{
    if (@available(macOS 11.0, iOS 14.0, *))
        return mgr.authorizationStatus;
#if TARGET_OS_IOS
    return [CLLocationManager authorizationStatus];
#else
    return kCLAuthorizationStatusNotDetermined;
#endif
}

static const mel_geo_auth* apple__auth_from_status(CLAuthorizationStatus st)
{
    if (st == kCLAuthorizationStatusAuthorizedAlways)
        return &mel_geo_auth_granted_always;
#if TARGET_OS_IOS
    if (st == kCLAuthorizationStatusAuthorizedWhenInUse)
        return &mel_geo_auth_granted_in_use;
#endif
    if (st == kCLAuthorizationStatusDenied)
        return &mel_geo_auth_denied;
    if (st == kCLAuthorizationStatusRestricted)
        return &mel_geo_auth_restricted;
    return &mel_geo_auth_not_determined;
}

static Mel_Geo_Fix apple__fix_from_location(CLLocation* loc)
{
    Mel_Geo_Fix fix = { 0 };
    if (loc.horizontalAccuracy >= 0.0)
    {
        fix.latitude_deg = loc.coordinate.latitude;
        fix.longitude_deg = loc.coordinate.longitude;
        fix.horizontal_accuracy_m = loc.horizontalAccuracy;
        fix.valid |= MEL_GEO_VALID_POSITION | MEL_GEO_VALID_HACC;
    }
    if (loc.verticalAccuracy > 0.0)
    {
        if (@available(macOS 12.0, iOS 15.0, *))
            fix.altitude_m = loc.ellipsoidalAltitude;
        else
            fix.altitude_m = loc.altitude;
        fix.vertical_accuracy_m = loc.verticalAccuracy;
        fix.valid |= MEL_GEO_VALID_ALTITUDE | MEL_GEO_VALID_VACC;
    }
    if (loc.speed >= 0.0)
    {
        fix.speed_mps = loc.speed;
        fix.valid |= MEL_GEO_VALID_SPEED;
    }
    if (loc.speedAccuracy >= 0.0)
    {
        fix.speed_accuracy_mps = loc.speedAccuracy;
        fix.valid |= MEL_GEO_VALID_SPEED_ACC;
    }
    if (loc.course >= 0.0)
    {
        fix.course_deg = loc.course;
        fix.valid |= MEL_GEO_VALID_COURSE;
    }
    if (@available(macOS 10.15.4, iOS 13.4, *))
    {
        if (loc.courseAccuracy >= 0.0)
        {
            fix.course_accuracy_deg = loc.courseAccuracy;
            fix.valid |= MEL_GEO_VALID_COURSE_ACC;
        }
    }
    fix.utc_unix_ms = (u64)([loc.timestamp timeIntervalSince1970] * 1000.0);
    fix.monotonic_ns = mel_nanos_since_unspecified_epoch();
    fix.valid |= MEL_GEO_VALID_UTC | MEL_GEO_VALID_MONOTONIC;
    return fix;
}

static CLLocationAccuracy apple__accuracy_tier(f64 accuracy_m)
{
    if (accuracy_m <= 5.0)
        return kCLLocationAccuracyBest;
    if (accuracy_m <= 10.0)
        return kCLLocationAccuracyNearestTenMeters;
    if (accuracy_m <= 100.0)
        return kCLLocationAccuracyHundredMeters;
    if (accuracy_m <= 1000.0)
        return kCLLocationAccuracyKilometer;
    return kCLLocationAccuracyThreeKilometers;
}

static void apple__oneshot_refresh(void)
{
    if (g_pending == NULL)
        return;
    f64 acc = 1e18;
    for (Mel_Geo_Request* req = g_pending; req != NULL; req = req->provider_next)
        if (req->accuracy_m < acc)
            acc = req->accuracy_m;
    g_oneshot_mgr.desiredAccuracy = apple__accuracy_tier(acc);
    [g_oneshot_mgr requestLocation];
}

static void apple__satisfy_pending(const Mel_Geo_Fix* fix)
{
    Mel_Geo_Request** pp = &g_pending;
    while (*pp != NULL)
    {
        Mel_Geo_Request* req = *pp;
        bool sharp = (fix->valid & MEL_GEO_VALID_HACC) && fix->horizontal_accuracy_m <= req->accuracy_m;
        if ((fix->valid & MEL_GEO_VALID_POSITION) && sharp)
        {
            *pp = req->provider_next;
            req->provider_next = NULL;
            g_sink->on_request(req, fix, &mel_geo_ok);
        }
        else
            pp = &req->provider_next;
    }
}

static void apple__fail_pending(const mel_geo_result* r)
{
    Mel_Geo_Request* req = g_pending;
    g_pending = NULL;
    while (req != NULL)
    {
        Mel_Geo_Request* next = req->provider_next;
        req->provider_next = NULL;
        g_sink->on_request(req, NULL, r);
        req = next;
    }
}

@implementation MelGeoDelegate

- (void)locationManager:(CLLocationManager*)manager didUpdateLocations:(NSArray<CLLocation*>*)locations
{
    CLLocation* loc = locations.lastObject;
    if (loc == nil)
        return;
    Mel_Geo_Fix fix = apple__fix_from_location(loc);
    if (manager == g_stream_mgr && g_streaming)
        g_sink->on_fix(&fix);
    apple__satisfy_pending(&fix);
    if (manager == g_oneshot_mgr && g_pending != NULL)
        apple__oneshot_refresh();
}

- (void)locationManager:(CLLocationManager*)manager didFailWithError:(NSError*)error
{
    if (error.domain != NSCocoaErrorDomain && [error.domain isEqualToString:kCLErrorDomain])
    {
        if (error.code == kCLErrorLocationUnknown)
            return;
        if (error.code == kCLErrorDenied)
        {
            if (manager == g_stream_mgr)
                g_sink->on_stream_result(&mel_geo_denied);
            if (manager == g_oneshot_mgr)
                apple__fail_pending(&mel_geo_denied);
            return;
        }
    }
    if (manager == g_stream_mgr)
        g_sink->on_stream_result(&mel_geo_unavailable);
    if (manager == g_oneshot_mgr)
        apple__fail_pending(&mel_geo_unavailable);
}

- (void)locationManagerDidChangeAuthorization:(CLLocationManager*)manager
{
    if (manager != g_stream_mgr)
        return;
    CLAuthorizationStatus st = apple__status(manager);
    if (st == kCLAuthorizationStatusNotDetermined)
        return;
    const mel_geo_auth* auth = apple__auth_from_status(st);
    Mel_Future*         f = g_auth_future;
    g_auth_future = NULL;
    g_sink->on_auth(f, auth);
}

#if TARGET_OS_IOS
- (void)locationManager:(CLLocationManager*)manager didUpdateHeading:(CLHeading*)newHeading
{
    Mel_Geo_Heading h = { 0 };
    if (newHeading.magneticHeading >= 0.0)
    {
        h.magnetic_deg = newHeading.magneticHeading;
        h.valid |= MEL_GEO_VALID_HEADING_MAGNETIC;
    }
    if (newHeading.trueHeading >= 0.0)
    {
        h.true_deg = newHeading.trueHeading;
        h.valid |= MEL_GEO_VALID_HEADING_TRUE;
    }
    if (newHeading.headingAccuracy >= 0.0)
    {
        h.accuracy_deg = newHeading.headingAccuracy;
        h.valid |= MEL_GEO_VALID_HEADING_ACC;
    }
    h.monotonic_ns = mel_nanos_since_unspecified_epoch();
    g_sink->on_heading(&h);
}
#endif

- (void)locationManager:(CLLocationManager*)manager didEnterRegion:(CLRegion*)region
{
    NSValue* v = g_region_nodes[region.identifier];
    if (v != nil)
        g_sink->on_region((Mel_Geo_Region*)v.pointerValue, true);
}

- (void)locationManager:(CLLocationManager*)manager didExitRegion:(CLRegion*)region
{
    NSValue* v = g_region_nodes[region.identifier];
    if (v != nil)
        g_sink->on_region((Mel_Geo_Region*)v.pointerValue, false);
}

- (void)locationManager:(CLLocationManager*)manager monitoringDidFailForRegion:(CLRegion*)region withError:(NSError*)error
{
    if (region == nil)
        return;
    NSValue* v = g_region_nodes[region.identifier];
    if (v == nil)
        return;
    mel_log_warn("geo", "region monitoring failed: %s", error.localizedDescription.UTF8String);
    g_sink->on_region_result((Mel_Geo_Region*)v.pointerValue, &mel_geo_exhausted);
}

@end

static bool apple_available(void* user)
{
    (void)user;
    return true;
}

static void apple_attach(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink)
{
    (void)user;
    (void)vat;
    mel_assert_msg("apple geolocation requires the home vat on the main thread", [NSThread isMainThread]);
    g_sink = sink;
    g_delegate = [[MelGeoDelegate alloc] init];
    g_stream_mgr = [[CLLocationManager alloc] init];
    g_stream_mgr.delegate = g_delegate;
    g_oneshot_mgr = [[CLLocationManager alloc] init];
    g_oneshot_mgr.delegate = g_delegate;
    g_region_nodes = [[NSMutableDictionary alloc] init];
}

static void apple_detach(void* user)
{
    (void)user;
    if (g_streaming)
        [g_stream_mgr stopUpdatingLocation];
    g_streaming = false;
    g_stream_mgr.delegate = nil;
    g_oneshot_mgr.delegate = nil;
    g_stream_mgr = nil;
    g_oneshot_mgr = nil;
    g_delegate = nil;
    g_region_nodes = nil;
    g_pending = NULL;
    g_auth_future = NULL;
    g_sink = NULL;
}

static Mel_Geo_Caps apple_caps(void* user)
{
    (void)user;
    Mel_Geo_Caps caps = {
        .fixes = true,
        .regions_native = [CLLocationManager isMonitoringAvailableForClass:[CLCircularRegion class]],
        .geocoding = true,
    };
#if TARGET_OS_IOS
    caps.heading = [CLLocationManager headingAvailable];
    caps.background = true;
#endif
    return caps;
}

static const mel_geo_auth* apple_authorization(void* user)
{
    (void)user;
    return apple__auth_from_status(apple__status(g_stream_mgr));
}

static bool apple__plist_has(NSString* key)
{
    return [[NSBundle mainBundle] objectForInfoDictionaryKey:key] != nil;
}

static void apple_authorize(void* user, const mel_geo_scope* scope, Mel_Future* future)
{
    (void)user;
    CLAuthorizationStatus st = apple__status(g_stream_mgr);
    if (st != kCLAuthorizationStatusNotDetermined)
    {
        g_sink->on_auth(future, apple__auth_from_status(st));
        return;
    }
    if (!apple__plist_has(@"NSLocationWhenInUseUsageDescription"))
    {
        mel_log_error("geo", "Info.plist is missing NSLocationWhenInUseUsageDescription; the OS will not prompt");
        g_sink->on_auth(future, &mel_geo_auth_denied);
        return;
    }
    mel_assert_msg("an authorize is already pending", g_auth_future == NULL);
    g_auth_future = future;
    if (scope == &mel_geo_scope_always)
    {
#if TARGET_OS_IOS
        if (!apple__plist_has(@"NSLocationAlwaysAndWhenInUseUsageDescription"))
        {
            mel_log_error("geo", "Info.plist is missing NSLocationAlwaysAndWhenInUseUsageDescription; the OS will not prompt");
            g_auth_future = NULL;
            g_sink->on_auth(future, &mel_geo_auth_denied);
            return;
        }
        [g_stream_mgr requestAlwaysAuthorization];
#else
        [g_stream_mgr requestWhenInUseAuthorization];
#endif
        return;
    }
    [g_stream_mgr requestWhenInUseAuthorization];
}

static const mel_geo_result* apple_last_known(void* user, Mel_Geo_Fix* out)
{
    (void)user;
    CLLocation* loc = g_stream_mgr.location != nil ? g_stream_mgr.location : g_oneshot_mgr.location;
    if (loc == nil)
        return &mel_geo_unavailable;
    *out = apple__fix_from_location(loc);
    return &mel_geo_ok;
}

static void apple_request(void* user, Mel_Geo_Request* req)
{
    (void)user;
    if (!mel_geo_auth_is_granted(apple__auth_from_status(apple__status(g_stream_mgr))))
    {
        g_sink->on_request(req, NULL, &mel_geo_denied);
        return;
    }
    req->provider_next = g_pending;
    g_pending = req;
    apple__oneshot_refresh();
}

static void apple_request_cancel(void* user, Mel_Geo_Request* req)
{
    (void)user;
    for (Mel_Geo_Request** pp = &g_pending; *pp != NULL; pp = &(*pp)->provider_next)
        if (*pp == req)
        {
            *pp = req->provider_next;
            break;
        }
    req->provider_next = NULL;
}

static const mel_geo_result* apple_stream_start(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    const mel_geo_auth* auth = apple__auth_from_status(apple__status(g_stream_mgr));
    if (auth == &mel_geo_auth_denied || auth == &mel_geo_auth_restricted)
        return &mel_geo_denied;
    if (auth == &mel_geo_auth_not_determined)
    {
        mel_log_warn("geo", "stream start before authorization; call mel_geo_authorize first");
        return &mel_geo_denied;
    }
    g_stream_mgr.desiredAccuracy = apple__accuracy_tier(d->accuracy_m);
    g_stream_mgr.distanceFilter = d->min_distance_m > 0.0 ? d->min_distance_m : kCLDistanceFilterNone;
    [g_stream_mgr startUpdatingLocation];
    g_streaming = true;
    return &mel_geo_ok;
}

static void apple_stream_update(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    g_stream_mgr.desiredAccuracy = apple__accuracy_tier(d->accuracy_m);
    g_stream_mgr.distanceFilter = d->min_distance_m > 0.0 ? d->min_distance_m : kCLDistanceFilterNone;
}

static void apple_stream_stop(void* user)
{
    (void)user;
    [g_stream_mgr stopUpdatingLocation];
    g_streaming = false;
}

#if TARGET_OS_IOS
static const mel_geo_result* apple_heading_start(void* user)
{
    (void)user;
    if (![CLLocationManager headingAvailable])
        return &mel_geo_unsupported;
    [g_stream_mgr startUpdatingHeading];
    return &mel_geo_ok;
}

static void apple_heading_stop(void* user)
{
    (void)user;
    [g_stream_mgr stopUpdatingHeading];
}
#endif

static const mel_geo_result* apple_region_add(void* user, Mel_Geo_Region* r)
{
    (void)user;
    NSString*         ident = [NSString stringWithFormat:@"melgeo-%p", (void*)r];
    CLCircularRegion* cr = [[CLCircularRegion alloc]
        initWithCenter:CLLocationCoordinate2DMake(r->latitude_deg, r->longitude_deg)
                radius:r->radius_m
            identifier:ident];
    cr.notifyOnEntry = r->notify_enter;
    cr.notifyOnExit = r->notify_exit;
    g_region_nodes[ident] = [NSValue valueWithPointer:r];
    r->native = (__bridge_retained void*)cr;
    [g_stream_mgr startMonitoringForRegion:cr];
    return &mel_geo_ok;
}

static void apple_region_remove(void* user, Mel_Geo_Region* r)
{
    (void)user;
    CLCircularRegion* cr = (__bridge_transfer CLCircularRegion*)r->native;
    r->native = NULL;
    if (cr == nil)
        return;
    [g_stream_mgr stopMonitoringForRegion:cr];
    [g_region_nodes removeObjectForKey:cr.identifier];
}

static str8 apple__place_str(NSString* s, const Mel_Alloc* alloc)
{
    if (s == nil || s.length == 0)
        return (str8){ 0 };
    const char* utf8 = s.UTF8String;
    return str8_dup_alloc((str8){ (u8*)utf8, strlen(utf8) }, alloc);
}

static void apple__geocode_finish(Mel_Geo_Geocode* g, NSArray<CLPlacemark*>* placemarks, NSError* error)
{
    if (!mel_geo_provider_geocode_claim(g))
        return;
    if (placemarks.count == 0 && error != nil && error.code != kCLErrorGeocodeFoundNoResult)
    {
        g_sink->on_geocode(g, &mel_geo_unavailable);
        return;
    }
    for (CLPlacemark* pm in placemarks)
    {
        if (g->places.count >= g->max_results)
            break;
        Mel_Geo_Place place = {
            .name = apple__place_str(pm.name, g->alloc),
            .thoroughfare = apple__place_str(pm.thoroughfare, g->alloc),
            .locality = apple__place_str(pm.locality, g->alloc),
            .admin_area = apple__place_str(pm.administrativeArea, g->alloc),
            .postal_code = apple__place_str(pm.postalCode, g->alloc),
            .country = apple__place_str(pm.country, g->alloc),
            .country_code = apple__place_str(pm.ISOcountryCode, g->alloc),
        };
        if (pm.location != nil)
        {
            place.latitude_deg = pm.location.coordinate.latitude;
            place.longitude_deg = pm.location.coordinate.longitude;
            place.valid |= MEL_GEO_VALID_POSITION;
        }
        mel_array_push(&g->places, place);
    }
    g_sink->on_geocode(g, &mel_geo_ok);
}

static void apple_geocode_forward(void* user, Mel_Geo_Geocode* g)
{
    (void)user;
    NSString* query = [[NSString alloc] initWithBytes:g->query.data length:g->query.len encoding:NSUTF8StringEncoding];
    CLGeocoder* coder = [[CLGeocoder alloc] init];
    [coder geocodeAddressString:query
              completionHandler:^(NSArray<CLPlacemark*>* placemarks, NSError* error) {
                  apple__geocode_finish(g, placemarks, error);
              }];
}

static void apple_geocode_reverse(void* user, Mel_Geo_Geocode* g)
{
    (void)user;
    CLLocation* loc = [[CLLocation alloc] initWithLatitude:g->latitude_deg longitude:g->longitude_deg];
    CLGeocoder* coder = [[CLGeocoder alloc] init];
    [coder reverseGeocodeLocation:loc
                completionHandler:^(NSArray<CLPlacemark*>* placemarks, NSError* error) {
                    apple__geocode_finish(g, placemarks, error);
                }];
}

void mel_geo__register_host_providers(void)
{
    static Mel_Geo_Provider_Node node;
    node.desc = (Mel_Geo_Provider_Desc){
        .name = "apple-corelocation",
        .available = apple_available,
        .attach = apple_attach,
        .detach = apple_detach,
        .caps = apple_caps,
        .authorization = apple_authorization,
        .authorize = apple_authorize,
        .last_known = apple_last_known,
        .request = apple_request,
        .request_cancel = apple_request_cancel,
        .stream_start = apple_stream_start,
        .stream_update = apple_stream_update,
        .stream_stop = apple_stream_stop,
        .geocode_forward = apple_geocode_forward,
        .geocode_reverse = apple_geocode_reverse,
    };
#if TARGET_OS_IOS
    node.desc.heading_start = apple_heading_start;
    node.desc.heading_stop = apple_heading_stop;
#endif
    if ([CLLocationManager isMonitoringAvailableForClass:[CLCircularRegion class]])
    {
        node.desc.region_add = apple_region_add;
        node.desc.region_remove = apple_region_remove;
    }
    mel_geo_provider_register(&node);
}
