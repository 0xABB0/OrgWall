#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection/array.h>
#include <executor/executor.h>
#include <future/future.h>

#include <stdatomic.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Vat Mel_Vat;

#define MEL_GEO_NEVER INT64_MAX

typedef struct mel_geo_auth mel_geo_auth;

extern const mel_geo_auth mel_geo_auth_granted_always;
extern const mel_geo_auth mel_geo_auth_granted_in_use;
extern const mel_geo_auth mel_geo_auth_denied;
extern const mel_geo_auth mel_geo_auth_restricted;
extern const mel_geo_auth mel_geo_auth_not_determined;

const char* mel_geo_auth_name(const mel_geo_auth* a);
bool        mel_geo_auth_is_granted(const mel_geo_auth* a);

typedef struct mel_geo_scope mel_geo_scope;

extern const mel_geo_scope mel_geo_scope_in_use;
extern const mel_geo_scope mel_geo_scope_always;

const char* mel_geo_scope_name(const mel_geo_scope* s);

typedef struct mel_geo_result mel_geo_result;

extern const mel_geo_result mel_geo_ok;
extern const mel_geo_result mel_geo_denied;
extern const mel_geo_result mel_geo_unavailable;
extern const mel_geo_result mel_geo_timeout;
extern const mel_geo_result mel_geo_cancelled;
extern const mel_geo_result mel_geo_unsupported;
extern const mel_geo_result mel_geo_lost;
extern const mel_geo_result mel_geo_exhausted;

const char* mel_geo_result_name(const mel_geo_result* r);
bool        mel_geo_result_ok(const mel_geo_result* r);

#define MEL_GEO_VALID_POSITION         (1u << 0)
#define MEL_GEO_VALID_ALTITUDE         (1u << 1)
#define MEL_GEO_VALID_HACC             (1u << 2)
#define MEL_GEO_VALID_VACC             (1u << 3)
#define MEL_GEO_VALID_SPEED            (1u << 4)
#define MEL_GEO_VALID_SPEED_ACC        (1u << 5)
#define MEL_GEO_VALID_COURSE           (1u << 6)
#define MEL_GEO_VALID_COURSE_ACC       (1u << 7)
#define MEL_GEO_VALID_UTC              (1u << 8)
#define MEL_GEO_VALID_MONOTONIC        (1u << 9)

#define MEL_GEO_VALID_HEADING_MAGNETIC (1u << 0)
#define MEL_GEO_VALID_HEADING_TRUE     (1u << 1)
#define MEL_GEO_VALID_HEADING_ACC      (1u << 2)

typedef struct
{
    f64 latitude_deg, longitude_deg;
    f64 altitude_m;
    f64 horizontal_accuracy_m, vertical_accuracy_m;
    f64 speed_mps, speed_accuracy_mps;
    f64 course_deg, course_accuracy_deg;
    u64 utc_unix_ms;
    u64 monotonic_ns;
    u32 valid;
} Mel_Geo_Fix;

typedef struct
{
    f64 magnetic_deg, true_deg, accuracy_deg;
    u64 monotonic_ns;
    u32 valid;
} Mel_Geo_Heading;

typedef struct
{
    bool fixes;
    bool heading;
    bool regions_native;
    bool geocoding;
    bool background;
} Mel_Geo_Caps;

typedef struct Mel_Geo_Request Mel_Geo_Request;
struct Mel_Geo_Request
{
    f64 accuracy_m;
    i64 timeout_ns;
    i64 max_age_ns;

    Mel_Future  future;
    Mel_Geo_Fix fix;

    Mel_Geo_Request*      next;
    Mel_Geo_Request*      provider_next;
    void*                 provider_data;
    i64                   deadline_ns;
    _Atomic(u32)          claimed;
    const mel_geo_result* done_result;
    Mel_Geo_Fix           done_fix;
    bool                  linked;
};

typedef void (*Mel_Geo_Fix_Fn)(const Mel_Geo_Fix* fix, const mel_geo_result* result, void* user);

typedef struct Mel_Geo_Watch Mel_Geo_Watch;
struct Mel_Geo_Watch
{
    f64 accuracy_m;
    i64 min_interval_ns;
    f64 min_distance_m;

    Mel_Geo_Fix_Fn cb;
    void*          user;
    Mel_Executor*  exec;

    Mel_Geo_Watch*        next;
    Mel_Task              task;
    _Atomic(u32)          live;
    _Atomic(u32)          pending_seq;
    Mel_Geo_Fix           pending_fix;
    const mel_geo_result* pending_result;
    Mel_Geo_Fix           last_fix;
    i64                   last_delivery_ns;
    bool                  has_last;
};

typedef void (*Mel_Geo_Heading_Fn)(const Mel_Geo_Heading* h, const mel_geo_result* result, void* user);

typedef struct Mel_Geo_Heading_Watch Mel_Geo_Heading_Watch;
struct Mel_Geo_Heading_Watch
{
    Mel_Geo_Heading_Fn cb;
    void*              user;
    Mel_Executor*      exec;

    Mel_Geo_Heading_Watch* next;
    Mel_Task               task;
    _Atomic(u32)           live;
    _Atomic(u32)           pending_seq;
    Mel_Geo_Heading        pending_heading;
    const mel_geo_result*  pending_result;
};

typedef struct Mel_Geo_Region Mel_Geo_Region;

typedef struct
{
    Mel_Geo_Region*       region;
    bool                  entered, exited;
    const mel_geo_result* result;
} Mel_Geo_Region_Event;

typedef void (*Mel_Geo_Region_Fn)(const Mel_Geo_Region_Event* ev, void* user);

struct Mel_Geo_Region
{
    f64  latitude_deg, longitude_deg, radius_m;
    bool notify_enter, notify_exit;

    Mel_Geo_Region_Fn cb;
    void*             user;
    Mel_Executor*     exec;

    Mel_Geo_Region*                next;
    Mel_Task                       task;
    _Atomic(u32)                   live;
    _Atomic(u32)                   pending_bits;
    _Atomic(const mel_geo_result*) pending_result;
    bool                           sw;
    bool                           seeded;
    bool                           inside;
    u64                            native_id;
    void*                          native;
};

typedef struct
{
    str8 name, thoroughfare, locality, admin_area, postal_code, country, country_code;
    f64  latitude_deg, longitude_deg;
    u32  valid;
} Mel_Geo_Place;

typedef Mel_Array(Mel_Geo_Place) Mel_Geo_Places;

typedef struct Mel_Geo_Geocode Mel_Geo_Geocode;
struct Mel_Geo_Geocode
{
    str8 query;
    f64  latitude_deg, longitude_deg;
    u32  max_results;

    const Mel_Alloc* alloc;
    Mel_Future       future;
    Mel_Geo_Places   places;

    Mel_Geo_Geocode*      next;
    _Atomic(u32)          claimed;
    const mel_geo_result* done_result;
    bool                  reverse;
    bool                  linked;
};

void         mel_geo_init(Mel_Vat* vat);
void         mel_geo_shutdown(void);
Mel_Geo_Caps mel_geo_caps(void);

const mel_geo_auth* mel_geo_authorization(void);
void                mel_geo_authorize(const mel_geo_scope* scope, Mel_Future* future);
const mel_geo_auth* mel_geo_future_auth(const Mel_Future* f);

const mel_geo_result* mel_geo_future_result(const Mel_Future* f);

const mel_geo_result* mel_geo_last_known(Mel_Geo_Fix* out);

void mel_geo_request(Mel_Geo_Request* req);
void mel_geo_request_cancel(Mel_Geo_Request* req);

const mel_geo_result* mel_geo_watch_start(Mel_Geo_Watch* w);
void                  mel_geo_watch_stop(Mel_Geo_Watch* w);

const mel_geo_result* mel_geo_heading_start(Mel_Geo_Heading_Watch* w);
void                  mel_geo_heading_stop(Mel_Geo_Heading_Watch* w);

const mel_geo_result* mel_geo_region_add(Mel_Geo_Region* r);
void                  mel_geo_region_remove(Mel_Geo_Region* r);

void mel_geo_geocode_forward(Mel_Geo_Geocode* g);
void mel_geo_geocode_reverse(Mel_Geo_Geocode* g);
void mel_geo_geocode_cancel(Mel_Geo_Geocode* g);
void mel_geo_geocode_free(Mel_Geo_Geocode* g);

f64 mel_geo_distance_m(f64 lat1_deg, f64 lon1_deg, f64 lat2_deg, f64 lon2_deg);

#ifdef __cplusplus
}
#endif
