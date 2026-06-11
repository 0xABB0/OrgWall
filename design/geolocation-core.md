# geolocation core — spec

Provider-independent half of `modules/geolocation` (see `geolocation.md` for
the surface). No prerequisite; ships with the mock provider and the
`geolocation-core` test target.

## Layout

    modules/geolocation/
        build.c  readme.md
        include/geolocation/geolocation.h     // public surface
        include/geolocation/provider.h        // backend contract
        src/geolocation.c                     // registry, marshalling, demand, regions, timeouts
        src/<platform>/...                    // backends (own specs)
        test/mock_provider.h|c                // registerable scripted provider
        test/geolocation_test.c

## State

One static core struct: home vat, active provider node, intrusive lists
(watches, heading watches, regions, pending requests, pending geocodes),
current demand, marshalling state, timeout source. All mutation owner-confined
(`mel_vat_is_owner` asserted on every public entry).

## Marshalling (backend thread → home vat)

Sink calls may arrive on any thread. The core keeps:

- a latest-wins fix slot + heading slot + stream-result slot (seqlock-style:
  version counter, write, version counter — readers retry on torn reads);
- per-region and per-request/geocode delivery rides each node's embedded
  `Mel_Task` directly (the backend already holds the node pointer);
- one embedded pump `Mel_Task` posted to `mel_vat_executor(home)` whenever a
  slot is written (atomic armed flag dedupes).

The pump task (owner thread) snapshots the slots, walks the watch list,
applies per-watch interval/distance filters, copies fix+result into each
watch's pending slot, posts each watch's task to its executor. Same for
heading. Region software evaluation runs here too. Zero allocation; everything
lives in core statics and caller nodes.

Per-node task: reads its pending slot, invokes `cb`. Latest-wins by
construction (slot overwritten, task re-armed at most once).

Unlink discipline: `watch_stop`/`region_remove`/`request_cancel` (owner
thread) unlink the node and disarm its task; a task that fires after disarm
sees the armed flag cleared and returns. A node is reusable after stop returns.

## Demand union

`demand.accuracy_m = min` over (watches, pending requests, software-region
demand); `min_interval_ns = min`; `min_distance_m = min`. Recomputed on every
add/remove; transitions drive `stream_start` / `stream_update` /
`stream_stop`. Software-region demand: `accuracy = clamp(smallest_radius / 2,
10 m, 1000 m)`, `interval = clamp(time-to-nearest-boundary at last speed, 1 s,
60 s)` recomputed per fix; no fix yet → 1 s until the first one lands.

## Requests

`mel_geo_request` validates, initialises the embedded future, links the node,
includes it in demand, and forwards to the provider's `request` op. Resolution
paths (first wins, by future CAS): backend `on_request`; core timeout; user
cancel. On any resolve the core unlinks, recomputes demand, and calls the
backend's `request_cancel` if the backend had it in flight. `max_age_ns > 0`:
core tries `last_known` first and resolves inline when fresh enough.

## Timeout source

One `Mel_Vat_Source` on the home vat: `deadline()` = min pending-request
deadline (else `MEL_VAT_NEVER`), `drain()` resolves expired requests. Deadlines
are absolute monotonic, stamped at `mel_geo_request`.

## Software region evaluation

For providers with `region_add == NULL`. Inside-state per region, symmetric
hysteresis:

    band    := min(max(hacc, 0.1 * radius), radius / 2)
    inside  := distance <= radius - band/2   (flip only from outside)
    outside := distance >= radius + band/2   (flip only from inside)

First fix after `region_add` seeds inside-state without firing; subsequent
flips fire per `notify_enter`/`notify_exit`. Distance is great-circle
(haversine, f64).

## Mock provider (test/)

Static node + script handles: `mock_geo_install()`, `mock_geo_push_fix(fix)`,
`mock_geo_push_stream_result(r)`, `mock_geo_set_auth(auth)`,
`mock_geo_resolve_request(fix, r)`, `mock_geo_set_caps(caps)`, counters for
stream_start/update/stop and last demand. Push entry points marshal exactly
like a real backend (callable from a test thread).

## Tests (geolocation-core)

- registry: install, init activates, caps pass through, shutdown detaches
- watch: start → demand starts stream; push fix → delivered on inline exec;
  interval + distance filters; latest-wins (push 3, pump once, see last)
- watch outage: denied result delivered with invalid fix, watch survives,
  ok resumes
- request: resolve via mock; cancel resolves cancelled; max_age served from
  last_known; timeout via stepping the vat until deadline source fires
- demand: two watches → min union; stop one → update; stop all → stream_stop
- regions (software): seed, enter, exit, hysteresis no-flap at boundary,
  notify flags respected, remove recomputes demand
- geocode: forward/reverse via mock fill, free releases via node alloc,
  unsupported when ops NULL
- validation: zero accuracy / NULL exec / double start assert-fail paths
  (death tests if the harness has them, else release-mode error returns)

## build.c

Library `geolocation`: `src/geolocation.c` ALWAYS, per-platform sources gated
by `WHEN(.platforms = ...)`, frameworks/libs per backend specs. Test target
`geolocation-core`: core + mock + runner; depends `test`, `geolocation`, and
the transitive set (`core allocator collection string future executor vat log
thread`).
