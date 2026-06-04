# locale — spec

The user's language preferences as the OS resolves them: an ordered list of preferred
locales, plus a change signal. Not a translation catalog, not a CLDR database, not a number/
date formatter — only *which languages, in what order, the user prefers*.

## Model

- **Ordered list, not a registry.** Preference order is the identity of an entry; rank 0 is
  the primary. Entries are not individually addressable across refreshes, so no per-entry
  generational handle (the `display` device spine would mis-model order as identity,
  MEL-ENGINE-IV). The list is a by-value snapshot.
- **Locale.** ISO-639 language (2–3 alpha, lowercased) plus optional ISO-3166 country
  (2 alpha, uppercased). A generic-language entry (no country) is valid. `tag` is the
  normalized `lang` or `lang-COUNTRY`; `language` and `country` slice it.
- **Provider.** One host provider per platform, registered at init (the `vibration`
  precedent). Yields raw tags in preference order; the core parses, normalizes, de-dups
  (first occurrence wins), and owns the strings.

## Status

`Mel_Locale_Status` is `u32`: severity mask `0x3` (`Ok | Warned | Error`) plus flag bits
(`Empty`, `No_Country`, `Tag_Normalized`, `Unavailable`, `Out_Of_Range`). Branch-free
`mel_locale_status_*` predicates. No error strings; the cause logs at the failure site
(MEL-ENGINE-VIII). `mel_locale_at`/`mel_locale_primary` return `{ value, status }`.

## Events

A change is one event carrying `changed_fields`, a flag bitset (not an enum, MEL-CODE-001):

- `Primary` — rank-0 tag changed (including empty⇄non-empty).
- `Membership` — the set of tags changed.
- `Order` — same set, different order.

Delivered through the `event` substrate: `mel_locale_subscribe(exec, cb, user)` (push) and
`mel_locale_poll_events` (pull) observe the same fire. The overflow policy is latest-wins
with a logged warning. `refresh` diffs the fresh list against the prior and fires once iff
anything moved; an idempotent refresh is silent.

## Refresh & change source

`mel_locale_refresh` re-probes the provider. Where the OS offers a native change signal
(macOS `NSCurrentLocaleDidChangeNotification`, web `languagechange`) the provider's `watch`
hook routes it to `mel_locale__on_change`, which refreshes — so the event fires without
polling. Platforms without a signal (linux env, win32, android) refresh on demand only; the
module spawns no thread and polls nothing on its own (MEL-ENGINE-III).

## Platform resolution

- **macos / ios** — `NSLocale.preferredLanguages` is already an ordered BCP-47 list.
- **linux** — POSIX has no ordered list; `$LANGUAGE` (GNU, colon-ordered) is the closest,
  then the single `$LC_ALL`/`$LC_MESSAGES`/`$LANG`. Codeset (`.UTF-8`) and modifier (`@euro`)
  are stripped; `C`/`POSIX` are not locales and are dropped.
- **android** — `LocaleList.getDefault()` is ordered (API 24+); each `Locale.toLanguageTag()`
  is BCP-47. Pre-24 falls back to the single `Locale.getDefault()`.
- **win32** — `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME)` is an ordered BCP-47 list.
- **wasm** — `navigator.languages` is ordered BCP-47; `navigator.language` is the singleton
  fallback.

## Ownership

Each snapshot entry's three `str8`s share one allocator-owned buffer, freed when the entry
leaves the list (refresh swap, shutdown). A provider allocates each raw tag with the
allocator the core passes to `enumerate`; the core frees the raw tag once it has parsed a
normalized copy. A too-small `out` buffer makes `enumerate` allocate nothing and return the
needed count; the core regrows and re-calls (no partial-write leak).

## Coding-guideline compliance

- No enums (MEL-CODE-001): status and `changed_fields` are severity-plus-bitset; there is one
  event kind, so no kind enum exists.
- No fixed arrays in owned state (MEL-CODE-002): the list and provider table are dynamic; the
  caller-sized `out`/`cap` snapshot is the only fixed surface, by value.
- Allocators (MEL-CODE-003): every allocating call takes a `Mel_Alloc*`; never `mel_malloc`.
- No silent defaults (MEL-CODE-007): empty list, out-of-range, and unparseable tags log and
  return an error/warning status.
