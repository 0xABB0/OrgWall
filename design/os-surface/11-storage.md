# Storage & filesystem — OS-surface atlas (finer grain)
> domains D11–D14. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

### D11 · fs — files, directories & metadata
def: persistent named byte storage and its namespace.
- **open/read/write**:
  · open flags & modes (create / excl / trunc / append / O_CLOEXEC / share-mode)
  · positional & vectored I/O (pread/pwrite · readv/writev · ReadFileScatter)
  · seek / tell / SEEK_HOLE/SEEK_DATA
  · truncate / extend / ftruncate
  · preallocate (fallocate / F_PREALLOCATE / SetFileValidData)
  · readahead / fadvise access-pattern hints (POSIX_FADV / FILE_FLAG_SEQUENTIAL)
- **directories**:
  · create / remove / rmdir
  · stream traversal (readdir / FindFirstFile / getdents)
  · dirent type without stat (d_type)
  · openat / *at relative-to-fd resolution (dirfd)
  · recursive walk & fts-style iteration
- **stat & metadata**:
  · size / block count / allocated vs logical
  · timestamps (atime / mtime / ctime / btime-birthtime · resolution)
  · type & inode / file-id (st_ino · BY_HANDLE file index · volume serial)
  · device & filesystem id (statfs / GetVolumeInformation)
  · stat-by-fd / by-handle / by-path-no-follow (lstat / fstatat)
- **permissions & ownership**:
  · POSIX mode bits & chmod / chown / umask
  · setuid/setgid/sticky
  · POSIX.1e & NFSv4 ACLs (acl_get / extended ACLs)
  · Windows DACL/SACL & security descriptors
  · effective-access query (access / faccessat / AuthZ)
- **links**:
  · symlinks (create / read / follow vs nofollow)
  · hardlinks (link / linkat · link count)
  · junctions & reparse points (win32)
  · canonicalize / realpath & symlink-loop limits
- **extended attributes**:
  · xattr get/set/list/remove (getxattr / fgetxattr)
  · namespaces (user / system / security / trusted)
  · resource forks & named streams (macOS `..namedfork` · NTFS ADS)
  · Finder/file flags (chflags · UF_HIDDEN · FILE_ATTRIBUTE_*)
- **locking**:
  · advisory whole-file (flock / LockFile)
  · advisory byte-range (fcntl F_SETLK / OFD locks / LockFileEx)
  · mandatory locking (where supported)
  · open-deny share modes (win32 sharing semantics)
- **sparse & holes**:
  · hole punching (FALLOC_FL_PUNCH_HOLE / FSCTL_SET_ZERO_DATA)
  · sparse-region query (FIEMAP / FSCTL_QUERY_ALLOCATED_RANGES)
  · zero-range
- **atomicity & durability**:
  · atomic rename / replace (rename / RenameEx / ReplaceFile)
  · atomic exchange / swap (renameat2 RENAME_EXCHANGE / FSExchangeData?)
  · noreplace rename (RENAME_NOREPLACE)
  · fsync / fdatasync / F_FULLFSYNC / FlushFileBuffers
  · directory fsync for entry durability
  · write barriers & ordering guarantees
- **temp & secure delete**:
  · temp-file creation (mkstemp / O_TMPFILE / GetTempFileName)
  · linkat-materialize anonymous temp
  · unlink-while-open semantics
  · secure / best-effort overwrite-unlink
- **path semantics**:
  · separators & root forms (drive / UNC / `\\?\` long-path)
  · case sensitivity / case preservation per volume
  · Unicode normalization (NFD on HFS+/APFS quirks)
  · max path / component length & limits query
  · reserved names & illegal chars (win32 CON/NUL…)
- **known/special folders**:
  · home / temp / cache / config / data dirs (XDG / FOLDERID / NSSearchPath)
  · current working directory get/set
  · app-relative & bundle resource roots
↑beyond: copy-on-write clones (APFS clonefile / FICLONE / ReFS block-clone), reflinks, snapshots, server-side copy (copy_file_range).
↓under: O_DIRECT / unbuffered I/O (FILE_FLAG_NO_BUFFERING), raw block devices, filesystem ioctls (FS_IOC_*), FUSE userspace filesystems.
apps: editors, databases, sync clients, build systems.
status: none.

### D12 · fs-sandbox — scoped & brokered file access
def: reaching user files under a sandbox where raw paths are denied.
- **pickers**:
  · open-file picker (single / multiple)
  · save / export picker & suggested name
  · folder / directory picker (UIDocumentPicker / SAF OPEN_DOCUMENT_TREE / IFileDialog / xdg-desktop-portal FileChooser / `showOpenFilePicker` / `showDirectoryPicker`)
  · type / extension / UTI / MIME filtering
  · default-location & start-directory hint
- **scoped grants**:
  · security-scoped bookmarks (create / resolve / stale detection)
  · start/stop accessing scoped resource (balanced begin/end)
  · persisted URI permission grants (SAF takePersistableUriPermission)
  · web FileSystemHandle permission query/request & persistence (IndexedDB-stored handles)
  · transient (this-launch) vs persisted lifetime
- **shared containers**:
  · app-group / shared container (macOS/iOS app-group · Android FileProvider authority)
  · group-scoped key paths
- **origin-private storage (web)**:
  · OPFS root & directory tree (navigator.storage getDirectory)
  · sync-access-file handles (worker-only fast path)
  · quota & persistence request (StorageManager persist / estimate)
- **content streams**:
  · content-URI open as stream (Android ContentResolver openInputStream)
  · FileProvider / NSFileProviderItem materialize-on-demand
  · cloud-placeholder / dataless-file fetch (NSURL isUbiquitous · win32 cloud-files API)
- **brokered raw access**:
  · portal-document-store fd handoff (xdg-document-portal)
  · broker-mediated path open outside sandbox profile
↑beyond: cloud-file provider extensions (publish as a provider), on-demand sync placeholders.
apps: any sandboxed editor/viewer on iOS / Mac App Store / Android / web.
status: none.

### D13 · fs-watch — change notification & volumes
def: being told when the filesystem changes, and what storage exists.
- **path watching**:
  · single-dir watch (inotify / ReadDirectoryChangesW / FileObserver / kqueue EVFILT_VNODE)
  · recursive subtree watch (FSEvents / ReadDirectoryChangesW recursive / SAF? )
  · event types (create / delete / modify / rename-from-to / attrib)
  · per-fd vs per-path registration & lifetime
- **delivery quality**:
  · event coalescing & debounce
  · ordering guarantees (or lack thereof)
  · overflow / queue-full signal (IN_Q_OVERFLOW / FSEvents kFSEventStreamEventFlagMustScanSubDirs)
  · historical / since-event-id replay (FSEvents event IDs)
  · latency / batching window tuning
- **volume lifecycle**:
  · mount / unmount events (DiskArbitration / udev / WM_DEVICECHANGE / StorageVolume broadcasts)
  · eject request & safe-removal
  · media insert / removal (removable)
  · network share connect / disconnect & reachability
- **storage enumeration**:
  · mounted volumes & mount points (getmntent / GetLogicalDrives / statvfs)
  · volume properties (name / fs-type / removable / read-only / internal-vs-external)
  · disk & partition enumeration (DADisk / lsblk-class / SetupDi)
  · network vs local classification
- **space & quota**:
  · free / total / available space (statvfs / GetDiskFreeSpaceEx)
  · per-user quota (where exposed)
  · low-space / important-vs-opportunistic capacity (NSURL volumeAvailableCapacityForImportantUsage)
  · low-disk-space notifications
- **trash**:
  · move-to-trash / recycle (NSFileManager trashItem / SHFileOperation / gio trash · XDG Trash spec)
  · restore-from-trash & original-location metadata
  · empty-trash boundary
apps: IDEs, sync clients, file managers, backup tools.
status: none.

### D14 · prefs — preferences & structured persistence
def: the OS-provided small-data stores an app uses for its own config.
- **key-value store**:
  · read / write / remove keys (NSUserDefaults / SharedPreferences / Jetpack DataStore / registry / GSettings / localStorage)
  · suite / domain / named-store selection
  · synchronous vs async commit (apply vs commit · DataStore flow)
  · batch / transactional write
- **typed values**:
  · scalars (bool / int / float / string)
  · arrays / dictionaries / nested containers
  · data / blob values
  · type coercion & registered-default fallback
- **change observation**:
  · per-key / whole-store change callbacks (KVO on defaults · OnSharedPreferenceChangeListener · GSettings::changed · `storage` event)
  · external / other-process change detection
- **scope**:
  · per-user vs per-machine (HKCU vs HKLM · /Library/Preferences vs ~)
  · per-app vs shared-suite / app-group
  · roaming vs local (win32 roaming registry / appdata)
  · sandbox-container-scoped store
- **defaults & management**:
  · register-defaults seeding (registerDefaults · default-value layering)
  · MDM / managed-configuration overlay (NSUserDefaults managed domain / Android RestrictionsManager / Group Policy)
  · read-only enforced / locked keys
  · factory-reset / clear-all
↑beyond: cloud-synced key-value store (NSUbiquitousKeyValueStore / web chrome.storage.sync-class) where treated as OS surface.
apps: every settings screen; first-run config.
status: none.
