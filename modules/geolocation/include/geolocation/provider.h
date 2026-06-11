#pragma once

#include <geolocation/geolocation.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    f64 accuracy_m;
    i64 min_interval_ns;
    f64 min_distance_m;
} Mel_Geo_Demand;

typedef struct
{
    void (*on_fix)(const Mel_Geo_Fix* fix);
    void (*on_stream_result)(const mel_geo_result* r);
    void (*on_heading)(const Mel_Geo_Heading* h);
    void (*on_region)(Mel_Geo_Region* region, bool entered);
    void (*on_region_result)(Mel_Geo_Region* region, const mel_geo_result* r);
    void (*on_auth)(Mel_Future* future, const mel_geo_auth* auth);
    void (*on_request)(Mel_Geo_Request* req, const Mel_Geo_Fix* fix, const mel_geo_result* r);
    void (*on_geocode)(Mel_Geo_Geocode* g, const mel_geo_result* r);
} Mel_Geo_Provider_Sink;

typedef struct
{
    const char* name;
    void*       user;

    bool (*available)(void* user);
    void (*attach)(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink);
    void (*detach)(void* user);
    Mel_Geo_Caps (*caps)(void* user);

    const mel_geo_auth* (*authorization)(void* user);
    void (*authorize)(void* user, const mel_geo_scope* scope, Mel_Future* future);

    const mel_geo_result* (*last_known)(void* user, Mel_Geo_Fix* out);
    void (*request)(void* user, Mel_Geo_Request* req);
    void (*request_cancel)(void* user, Mel_Geo_Request* req);

    const mel_geo_result* (*stream_start)(void* user, const Mel_Geo_Demand* d);
    void (*stream_update)(void* user, const Mel_Geo_Demand* d);
    void (*stream_stop)(void* user);

    const mel_geo_result* (*heading_start)(void* user);
    void (*heading_stop)(void* user);

    const mel_geo_result* (*region_add)(void* user, Mel_Geo_Region* r);
    void (*region_remove)(void* user, Mel_Geo_Region* r);

    void (*geocode_forward)(void* user, Mel_Geo_Geocode* g);
    void (*geocode_reverse)(void* user, Mel_Geo_Geocode* g);
} Mel_Geo_Provider_Desc;

typedef struct Mel_Geo_Provider_Node Mel_Geo_Provider_Node;
struct Mel_Geo_Provider_Node
{
    Mel_Geo_Provider_Desc  desc;
    Mel_Geo_Provider_Node* next;
};

void mel_geo_provider_register(Mel_Geo_Provider_Node* node);
void mel_geo_provider_unregister(Mel_Geo_Provider_Node* node);

bool mel_geo_provider_geocode_claim(Mel_Geo_Geocode* g);

void mel_geo__register_host_providers(void);

#ifdef __cplusplus
}
#endif
