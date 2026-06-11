#pragma once

#include <geolocation/provider.h>

void mock_geo_install(void);
void mock_geo_uninstall(void);

void mock_geo_set_available(bool v);
void mock_geo_set_caps(Mel_Geo_Caps caps);
void mock_geo_set_auth(const mel_geo_auth* a);
void mock_geo_set_start_result(const mel_geo_result* r);
void mock_geo_set_last_known(const Mel_Geo_Fix* fix);

void mock_geo_push_fix(const Mel_Geo_Fix* fix);
void mock_geo_push_stream_result(const mel_geo_result* r);
void mock_geo_push_heading(const Mel_Geo_Heading* h);

Mel_Geo_Request* mock_geo_pending_request(void);
void             mock_geo_resolve_request(const Mel_Geo_Fix* fix, const mel_geo_result* r);

Mel_Geo_Geocode* mock_geo_pending_geocode(void);
void             mock_geo_fill_geocode(u32 count);

u32            mock_geo_stream_starts(void);
u32            mock_geo_stream_updates(void);
u32            mock_geo_stream_stops(void);
u32            mock_geo_request_calls(void);
u32            mock_geo_request_cancels(void);
bool           mock_geo_streaming(void);
Mel_Geo_Demand mock_geo_demand(void);
