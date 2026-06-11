# geolocation: fix provider registry wipe on shutdown

## Work done

`mel_geo_shutdown` previously called `memset(&g_geo, 0, sizeof g_geo)`, zeroing the entire
`Mel_Geo_State` including `g_geo.providers`. Any provider registered externally before the first
`mel_geo_init` — or between an init/shutdown cycle — was silently dropped; only host providers
survived because they re-register on every `mel_geo_init`.

**Changes:**

- `include/geolocation/provider.h`: added `bool host` field to `Mel_Geo_Provider_Node`; added
  declaration for `mel_geo_provider_register_host`.

- `src/geolocation.c`:
  - Added `mel_geo_provider_register_host`: sets `node->host = true` then delegates to
    `mel_geo_provider_register`.
  - `mel_geo_shutdown`: before the `memset`, walks `g_geo.providers`, zeroes every node's `next`
    (so host static nodes are clean for re-registration), collects the non-host nodes into
    `external_head`, then restores `g_geo.providers = external_head` after the memset.

- All five platform `mel_geo__register_host_providers` implementations (`apple`, `android`×2,
  `linux`, `win32`, `wasm`) changed from `mel_geo_provider_register` to
  `mel_geo_provider_register_host`.

- `test/geolocation_test.c`: added `geolocation.external_provider_survives_reinit` — registers
  the mock provider, init/shutdown/init, asserts caps still pass through on the second init.

**Test run:** 19 passed, 0 failed, 0 skipped.

## Kludges

None.

## CLAUDE.md suggestions

None.

## Suggestions

The `mel_geo_init` guard (`g_geo.vat == NULL`) means calling init twice without a shutdown is
caught by assertion. The symmetric shutdown guard (`g_geo.vat == NULL` early-return) means double
shutdown is silently tolerated. These are both correct and consistent with the rest of the
framework.
