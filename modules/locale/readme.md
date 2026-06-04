# locale

User language preferences: the ordered, OS-resolved list of preferred locales, plus a
change signal. A locale is an ISO-639 language with an optional ISO-3166 country
(`en-US`, `fr-FR`, or a generic-language `de`), held in user preference order — the order
*is* the semantics, so the surface is an ordered list, not a handle registry.

## Surface

- `mel_locale_init(alloc)` / `mel_locale_init_ex(alloc, exec)` / `mel_locale_shutdown()`.
- `mel_locale_refresh()` — re-probe the host provider; returns the count; emits a diffed
  change event when the list moved.
- `mel_locale_count()` / `mel_locale_list(out, cap)` — the ordered snapshot.
- `mel_locale_at(i)` / `mel_locale_primary()` — `{ value, status }`; status is severity
  (`Ok | Warned | Error`) plus a flag bitset (`Empty`, `No_Country`, `Out_Of_Range`, …),
  never an error string (MEL-CODE-001).
- `mel_locale_subscribe(exec, cb, user)` + `mel_locale_poll_events(out, cap)` — the same
  push/pull event substrate the `display` registry uses. `Mel_Locale_Event.changed_fields`
  is a flag bitset (`Primary | Order | Membership`).

`str8` codes are owned by the module allocator; a snapshot entry's `tag`/`language`/`country`
slice one backing buffer.

## Backends

- **macos / ios** — `NSLocale.preferredLanguages`; `NSCurrentLocaleDidChangeNotification`
  drives `watch`.
- **linux** — `$LANGUAGE` (colon-ordered) then `$LC_ALL` / `$LC_MESSAGES` / `$LANG`, with
  codeset/modifier stripped; `C`/`POSIX` dropped.
- **android** — JNI `android.os.LocaleList.getDefault()` iterated to `toLanguageTag()`,
  falling back to `java.util.Locale.getDefault()`; the `platform` JNI bridge supplies the env.
- **win32** — `GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, …)`.
- **wasm** — `navigator.languages` (then `navigator.language`); the `languagechange` event
  drives `watch`.

## Provider contract

A provider's `enumerate(user, alloc, out, cap)` writes raw BCP-47-ish tags into `out`,
allocating each `tag` with the passed `alloc` (the core frees it after parsing). If the true
count exceeds `cap` it must allocate nothing and return the needed count; the core regrows
and re-calls. Optional `watch`/`unwatch` wire a native change signal to `mel_locale__on_change`.

## Dependencies

`core`, `allocator`, `collection`, `string`, `event`, `executor`, `log`; `platform` on android.
