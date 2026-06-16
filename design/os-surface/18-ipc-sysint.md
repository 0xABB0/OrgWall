# IPC, inter-app & system integration — OS-surface atlas (finer grain)
> domains D50–D61. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

---

## Band XI — IPC & inter-app

### D50 · ipc-local — pipes, queues & shared memory
def: moving bytes between processes on the same machine.
- **byte channels**: anonymous pipe · named pipe / FIFO · mailslot (datagram)
- **uds**: · stream · datagram · seqpacket · abstract namespace (linux)
- **uds-ancillary**: · fd passing (SCM_RIGHTS / WSADuplicateHandle) · credential passing (SO_PEERCRED / LOCAL_PEERCRED / SO_PEERTOKEN) · pid/uid/gid attestation
- **message queues**: · POSIX mq · SysV msg · queue priority & notify
- **shared memory**: shm_open · CreateFileMapping · ashmem / memfd · mmap-backed channel
- **shm-protocol**: · ring buffer (SPSC / MPSC) · lock-free queue · seqlock snapshot
- **cross-process sync**: named mutex / semaphore / event · futex over shm · doors (solaris-class) ?
↑beyond: huge-page shm channels · GPU-importable shm (dmabuf bridge, see D21)
apps: multi-process apps, sandbox brokers, sidecars, IPC libraries.
status: spawn (`channel`/`port` domains).

### D51 · ipc-rpc — system buses & object RPC
def: the OS's structured cross-process request/notify fabrics.
- **bus transport**: D-Bus session · D-Bus system · (mach ports beneath XPC) · Binder driver
- **dbus**: · method call/reply · signals · properties · introspection · name ownership & activation
- **xpc**: · connection & service · message dictionaries · activation (launchd) · mach-port send/recv rights
- **binder**: · AIDL interface · system service lookup (ServiceManager) · oneway vs sync · death-recipient
- **com-winrt**: · COM class activation & apartments · WinRT activation factory · proxy/stub marshalling · out-of-proc server
- **capability transfer**: · fd / handle over bus · mach send-right · binder strong/weak ref
- **service registry**: · bus name registration · discovery & enumeration · activation-on-demand
- **broadcast notify**: distributed notifications (CFNotificationCenter Darwin) · D-Bus signals
↓under: raw mach-message MIG · raw Binder ioctl (`/dev/binder`)
apps: desktop integration, privileged helpers, plugin sandboxes, system agents.
status: none.

### D52 · interapp — deep links, intents & sharing
def: launching and exchanging data with *other* apps.
- **deep links**: · custom URL scheme · universal link / app link (verified domain) · web `navigator` / registerProtocolHandler ?
- **intents**: · explicit · implicit + intent filter · activity result · pending intent · broadcast
- **share-out**: share sheet (UIActivityViewController) · `ACTION_SEND` / `SEND_MULTIPLE` · Windows Share contract
- **share-in (targets)**: · share extension · share-target registration · direct-share / sharing shortcuts
- **providers**: document provider · FileProvider · content provider / resolver
- **extensions**: app extension / activity · action extension · app clip / instant app ?
- **type negotiation**: UTI · MIME · declared exported/imported types · type-coercion preference
- **default-handler**: · scheme/type association · set-as-default flow · query current default
- **callback flows**: x-callback-url · activity result return · source-app attribution
apps: share-heavy apps, launchers, editors, integration hubs.
status: none.

### D53 · clipboard — pasteboard & drag-and-drop
def: user-driven transient data exchange.
- **representations**: text · rich (RTF/HTML) · image · file URL/ref · custom UTI/MIME · multiple flavors per item
- **multi-item**: · item array · per-item flavor set · index access
- **promised data**: · lazy / promised provider (NSFilePromise / DataObject deferred) · render-on-demand callback
- **ownership**: change-count / sequence number · ownership-lost notification · clear
- **named boards**: · general/default · find/font (named) · private app-scoped pasteboard
- **privacy**: · sensitive / concealed marking (no-history hint) · transient / auto-clear · paste-access prompt awareness
- **drag source**: · begin drag · drag image / preview · allowed operations (copy/move/link) · multi-item drag
- **drop target**: · drag-enter/over/leave/perform · operation negotiation · hit-test & feedback · concurrent flavor query
- **dnd scope**: intra-app · inter-app · inter-window
apps: editors, file managers, browsers, design tools.
status: spawn (`clipboard` domain).

### D54 · dnd-files — folded into D53 clipboard (drag-and-drop)

---

## Band XII — System integration & shell

### D55 · notification — local & push notifications
def: surfacing alerts through the OS notification system.
- **local**: · immediate post · scheduled (calendar / interval) · location-triggered · repeat
- **rich content**: · image / video attachment · custom content extension · inline reply · action buttons
- **categories**: · category / channel definition · importance / priority level · sound & vibration config
- **grouping**: · thread / group id · summary · collapse · badge count
- **push registration**: APNs token · FCM token · WNS channel · Web Push (VAPID) subscription
- **push delivery**: · alert vs silent/background push · priority & collapse-id · payload-triggered wake (background handler)
- **ongoing**: Live Activity · foreground-service notification · media/transport · progress
- **delivery policy**: · DND / focus awareness · interruption level (passive→critical) · time-sensitive · scheduled-summary
- **permission**: · request & status · provisional auth · settings deep-link
↑beyond: critical / time-sensitive alerts · push priority & collapse · server-side send fabric
apps: messengers, calendars, news, delivery, any background-updating app.
status: spawn (`notification` domain; `design/notification*.md`, `push-*.md`).

### D56 · background — background execution & scheduled work
def: doing work while not in the foreground, under OS budgets.
- **short tasks**: · begin/end background task (beginBackgroundTask) · finite grace window · expiration handler
- **deferred jobs**: BGTaskScheduler · WorkManager / JobScheduler · Task Scheduler · launchd agent/daemon · cron · systemd-timer
- **job constraints**: · network-required / unmetered · charging / battery-not-low · device-idle · storage-not-low
- **periodic**: · interval / refresh task · processing task · flex window & coalescing
- **push-wake**: · silent-push trigger · high-priority FCM wake · background fetch
- **foreground service**: · service type declaration · mandatory notification · start-from-background limits
- **wake control**: wake lock · idle-prevention assertion · screen-on request
- **budget**: · quota / app-standby bucket · runtime budget reporting · throttle awareness
- **autostart**: · launch-on-login / boot · boot-completed receiver · login item / launch agent
apps: sync, backup, downloaders, fitness trackers, automation.
status: none.

### D57 · packaging — install, entitlements & sandbox model
def: how the app is packaged, signed, permitted, and constrained — surfaced as queryable facts.
- **code signing**: · signature & identity · notarization (Darwin) · hardened runtime · APK/AAB signing scheme · Authenticode
- **entitlements**: · declared entitlements / capabilities · requested permissions (manifest) · query granted set
- **sandbox**: · profile / container layout · group container · sandbox-escape boundary awareness
- **app metadata**: · bundle/package id · version & build number · display name · query at runtime
- **install state**: · install source / installer package name · first-run detection · update / version-change detection
- **feature declarations**: `<uses-feature>` · Info.plist usage strings · appx / MSIX capabilities · required-device-capabilities
- **managed config**: · MDM / app-config (managed app configuration) · restrictions · per-app VPN/config push
- **store policy**: · store-imposed constraints surfaced to app · sideload / unknown-source state ?
apps: every shipping app (this is mostly build-time, queried at runtime).
status: none.

### D58 · docmodel — documents, thumbnails & associations
def: the OS document ecosystem an app plugs into.
- **open/recent**: · recent documents list · open-document restoration · jump-list / recent-items integration
- **associations**: · file-type association · default-app registration · set-as-default / query-default flow
- **thumbnails**: QLThumbnailProvider · IThumbnailProvider · Android thumbnailer · GNOME thumbnailer · preview/Quick Look generation
- **indexer metadata**: Spotlight / Core Spotlight · Windows Property/Search indexer · Android MediaStore contribution · searchable attributes
- **document state**: · autosave / versions · document-modified / dirty state · conflict & duplicate
- **export/print**: · print-to-PDF · "open in" / send-a-copy · export representations
apps: editors, viewers, creative tools.
status: none.

### D59 · shell-cli — terminal, TTY & process environment
def: the command-line execution surface (the CLI-app axis of the framework).
- **invocation**: argv · exit codes · environment variables · current working dir
- **std streams**: stdin/stdout/stderr · redirection · pipes · isatty detection per stream
- **tty modes**: · cooked / canonical · raw · cbreak · echo & line-discipline flags (termios / SetConsoleMode)
- **size & resize**: · terminal columns/rows query · SIGWINCH / resize event
- **vt capability**: · ANSI / VT sequences · truecolor (24-bit) · mouse reporting · bracketed paste · terminal capability query (terminfo / `$TERM`)
- **screen control**: · cursor move/show/hide · alternate screen buffer · scroll region · clear
- **pty**: · PTY master/slave allocation (openpty / ConPTY) · child attach
- **job control**: · process groups · foreground/background · SIGINT / SIGTSTP / SIGCONT · session leader
- **line editing**: · readline-style edit surface · history · completion hook
apps: shells, REPLs, TUIs, build tools, CLI utilities.
status: spawn (`shell`/`repl` domains; `apps/repl`).

### D60 · sysconfig — locale, theme & system settings
def: user/system configuration the app reads to adapt.
- **locale**: · language · region · preferred-language list · locale identifier
- **appearance**: · dark / light · accent color · high-contrast / increase-contrast
- **motion & transparency**: · reduce-motion · reduce-transparency · auto-play / animation prefs
- **text**: · dynamic-type / text-size · bold-text · font scaling
- **format prefs**: · 24-hour vs 12-hour · measurement units · first-day-of-week ?
- **layout direction**: · RTL / LTR · force-RTL awareness
- **defaults**: · default browser · default app per type (mirror of D58)
- **a11y state**: · screen-reader running · accessibility-settings flags (cross-ref D70)
- **change notifications**: · per-setting change broadcast · effective-config-changed event
apps: every adaptive UI; accessibility-first apps.
status: spawn (`locale` domain).

### D61 · i18n — internationalization data & algorithms
def: locale-correct text processing (OS-provided ICU/CLDR-class services).
- **collation**: · locale-aware sort · sort keys · strength / case ordering · numeric collation
- **number format**: · decimal · currency · percent · grouping & rounding · spell-out / ordinal ?
- **date/time format**: · date · time · interval · relative ("3 days ago") · skeleton / pattern
- **calendars**: · Gregorian · non-Gregorian (Islamic / Hebrew / Japanese / Buddhist) · era & field arithmetic
- **message format**: · plural selection · gender / select · named arguments · nested
- **case**: · upper / lower / title · locale-sensitive (Turkish i) · case folding
- **normalization**: · NFC / NFD / NFKC / NFKD · canonical equivalence
- **segmentation**: · grapheme cluster · word · line break · sentence
- **bidi & shaping**: · bidi reordering / levels · shaping handoff (boundary, see input-text D28)
- **transliteration**: · script transliteration · transform rules ?
- **locale data**: · available locales · locale display names · CLDR data query
apps: every localized app; document & text tools.
status: spawn (`locale`/`string` domains).
