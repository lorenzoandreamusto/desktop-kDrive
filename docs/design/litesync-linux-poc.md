# Lite Sync on Linux — Proof-of-Concept Design

## Purpose

This document describes the design of a proof of concept (POC) that brings Lite Sync
(on-demand placeholder files) to the Linux desktop client.

It provides:

- the current Lite Sync architecture and the platform gates that block Linux
- the architectural decisions the POC depends on
- the complete inventory of new and modified files
- a phased implementation plan with acceptance criteria per phase
- the test strategy, packaging consequences and known risks

The POC is exploratory: it must prove the technical feasibility of on-demand hydration on
Linux without committing the product to a specific UX.

---

## 1. Status and Scope

| Item | Value |
|---|---|
| Status | Phases 1–5 implemented; phase 6 implemented for the legacy Qt GUI only (the Linux v4 GUI is not on `develop`). Build and tests not yet verified in CI. |
| Target platform | Linux desktop, Ubuntu 22.04/24.04 amd64 first, ARM later |
| Base branch | `develop` (legacy Qt GUI) unless the QML GUI is in scope, then `protected/linux-v4` |
| Delivery | Feature flagged, off by default |
| Production readiness | Out of scope |

### In scope

- A loadable Linux VFS plugin selected through `VirtualFileMode::Linux`.
- A FUSE-based mount that presents dehydrated placeholders in the sync folder.
- Hydration on file open and dehydration on explicit request.
- Correct interaction with the sync engine (upload/edit/delete/move) while the mount is active.
- Activation and deactivation of Lite Sync on an existing sync.
- Minimal UI entry points in the legacy Qt GUI, then in the Linux v4 GUI.

### Out of scope

- File-manager integration (status badges, context-menu actions in Nautilus/Dolphin).
- Thumbnails, pin-state UI in the file manager, application exclusion UI.
- Windows/macOS behaviour changes, migrations of existing macOS/Windows databases.
- Performance tuning, large-scale validation, telemetry dashboards.
- Flatpak/Snap sandbox support.

---

## 2. Current State

Lite Sync exists for macOS (FileProvider extension) and Windows (cfapi). Everything else
is platform-agnostic and already routes through the `KDC::Vfs` interface.

### 2.1 Selection pipeline

1. `VirtualFileMode { Off, Win, Mac, EnumEnd }` — `src/libcommon/utility/cstypes.h:326`.
   The value is stored as an integer in the `sync` table and serialized to the GUIs.
2. Mode strings are stable identifiers used by configuration and the database:
   `Vfs::modeToString` / `Vfs::modeFromString` — `src/libcommonserver/vfs/vfs.cpp:96`, `:113`.
3. Plugin discovery: `modeToPluginName` and `isVfsPluginAvailable` load
   `kdrive_vfs_<name>` through `QPluginLoader` with IID `org.kdrive.PluginFactory` and
   shared metadata `src/server/vfs/vfspluginmetadata.json.in` —
   `src/libcommonserver/vfs/vfs.cpp:265`, `:271`.
4. `bestAvailableVfsMode()` and `createVfsFromPlugin()` —
   `src/libcommonserver/vfs/vfs.cpp:327`, `:337`.
5. Only two plugin targets are built: `src/server/vfs/CMakeLists.txt:3` adds `"win"` for
   MSVC and `"mac"` for APPLE; there is no Linux branch.

### 2.2 Server orchestration

| Concern | Location |
|---|---|
| Plugin availability log at startup | `src/server/appserver.cpp:431` |
| Legacy best-mode request | `src/server/appserver.cpp:2354` |
| Create and start VFS for a sync | `AppServer::tryCreateAndStartVfs` — `src/server/appserver.cpp:3323` |
| Extension installation (macOS only) | `AppServer::installVfs` — `src/server/appserver.cpp:4250` |
| Enable/disable Lite Sync on a sync | `AppServer::setSupportsVirtualFiles` — `src/server/appserver.cpp:4475`, mode choice at `:4506` |
| Stop VFS | `AppServer::stopVfs` — `src/server/appserver.cpp:4450` |

### 2.3 Persistence and GUI contracts

- `sync.supportVfs` and `sync.virtualFileMode` columns:
  `src/libparms/db/parmsdb.cpp:316`, bind at `:2525`.
- `supportVfs` is computed as `isNTFS || isAPFS` when creating a sync —
  `src/server/requests/serverrequests.cpp:835` — and in the legacy migration —
  `src/server/migration/migrationparams.cpp:413`.
- `virtualFileMode` is forced to `Off` on Linux — `src/server/requests/serverrequests.cpp:843`.
- GUI to server: `liteSync` boolean in the add-sync job —
  `src/server/comm/guijobs/abstractsyncaddjob.cpp:35`; best-mode request
  `UTILITY_BESTVFSAVAILABLEMODE` (`src/libcommon/comm.h:118`) handled by
  `src/server/comm/guijobs/utilitybestvfsavailablemodejob.cpp:58`, which applies
  `bestAvailableVfsMode()` and then filters by `CommonUtility::isLiteSyncCompatible(path)`.

### 2.4 Sync engine

The engine is platform-agnostic through `Vfs`:

- `isLiteSyncActivated()` = `vfsMode() != VirtualFileMode::Off` —
  `src/libsyncengine/propagation/executor/executorworker.h:125`.
- Placeholder decisions: `checkLiteSyncInfoForCreate` / `checkLiteSyncInfoForEdit`,
  `createPlaceholder`, `convertToPlaceholder` (`src/libsyncengine/propagation/executor/executorworker.cpp:713`),
  `dehydratePlaceholder`, `forceStatus`.
- Local deletes, blacklisting and conflict handling already branch on `vfsMode()`.

Three places remain platform-specific and must be extended for Linux:

| Location | Current behaviour |
|---|---|
| `src/libsyncengine/propagation/executor/executorworker.cpp:713` | `convertToPlaceholder` fills `dehydrated` for macOS and `localNodeId` for Windows, nothing for Linux |
| `src/libsyncengine/update_detection/file_system_observer/localfilesystemobserverworker.cpp:399` | special handling when `vfsMode() == Mac` |
| `src/libsyncengine/jobs/network/kDrive_API/downloadjob.cpp:393` | sparse-file check based on `isNTFS` |

### 2.5 IO and metadata

- Placeholder attributes are macOS-only: `namespace litesync_attrs` is compiled under
  `KD_MACOS` — `src/libcommonserver/io/iohelper.h:34`; `IoHelper::statusXAttr` at `:477`.
- `IoHelper::checkIfFileIsDehydrated` reads the `user.kDrive.status` extended attribute on
  Linux (`src/libcommonserver/io/iohelper_linux.cpp`), like the macOS implementation reads
  its own attribute.
- `CommonUtility::isLiteSyncCompatible` returns `false` on Linux —
  `src/libcommon/utility/utility.cpp:363`.

### 2.6 Exclusions and packaging

- `litesync-exclude.lst` contains macOS application names and is installed only in the
  macOS bundle — `CMakeLists.txt:271`; the file name is referenced by
  `src/libcommonserver/utility/utility.cpp:66` and `src/server/migration/migrationparams.cpp:92`.
- The Linux server installs plugins in `PLUGINDIR`, already defined as
  `${CMAKE_INSTALL_FULL_LIBDIR}/kdrive/plugins` — `CMakeLists.txt:116`.
- Dependencies are declared in `conanfile.py` (`requirements()`); Linux packaging scripts
  live in `infomaniak-build-tools/linux/`.

### 2.7 Hard gates that reject Linux today

| Gate | Location |
|---|---|
| Plugin name mapping returns empty for any non-Win/Mac mode | `src/libcommonserver/vfs/vfs.cpp:265` |
| Plugin availability check wrapped in `#if !defined(KD_LINUX)` | `src/libcommonserver/vfs/vfs.cpp:274` |
| Filesystem compatibility returns `false` in the `#else` branch | `src/libcommon/utility/utility.cpp:363` |
| Sync mode forced to `Off` | `src/server/requests/serverrequests.cpp:843` |
| Dehydration detection | Implemented through the Linux `user.kDrive.status` attribute (`src/libcommonserver/io/iohelper_linux.cpp`) |
| Linux v4 GUI forces `request.liteSync = false` | `src/gui4/linux/app/onboarding/onboardingsynccreationcoordinator.cpp:163`, `src/gui4/linux/app/settings/advancedsynccreationcontroller.cpp:263`, `src/gui4/linux/app/settings/settingssyncactivationcontroller.cpp:342` (branch `protected/linux-v4`) |

---

## 3. Design Decisions

### D1 — FUSE, not a fake placeholder

Two options were considered:

| Option | Description | Verdict |
|---|---|---|
| A. FUSE | A userspace filesystem mounted on the sync folder intercepts `open`/`read` and triggers a download | **Chosen** |
| B. Zero-byte files plus xattrs | Real empty files with a `user.*` status attribute; hydration only on an explicit action or a best-effort watcher | Rejected |

Option B cannot hydrate transparently when an application opens a file: the process sees
an empty file before any watcher can react. It would only demonstrate metadata plumbing,
not the product behaviour. Option A is the only way to validate on-demand hydration.

### D2 — Plugin boundary stays unchanged

The Linux implementation must satisfy the existing `KDC::Vfs` contract
(`src/libcommonserver/vfs/vfs.h`). No sync-engine bypass, no direct FUSE calls from the
server: the plugin owns the mount and the hydration queue, exactly like `VfsMac` owns the
extension connection and `VfsWin` owns the cfapi DLL.

### D3 — Placeholder metadata storage

Decision for the POC: store status and pin state as extended attributes in the `user.*`
namespace on the placeholder inode, mirroring the macOS value vocabulary
(`O` online, `F` offline, `H<progress>` hydrating, `U`/`P`/`E` pin states). Phase 2 also
stores `user.kDrive.size`, `user.kDrive.mtime` and `user.kDrive.nodeid` so the FUSE layer
can report the logical size and the sync engine can recover metadata after a restart.

Rationale: keeps `status()`, `isDehydratedPlaceholder()` and `setPinState()` cheap and
survives server restarts. If FUSE inode identity makes xattrs unreliable, the fallback is
an in-memory/on-disk map owned by the plugin; the `Vfs` contract does not change.

### D4 — Filesystem policy

Lite Sync on Linux is allowed only on filesystems that support sparse files and robust
xattrs: ext4, xfs and btrfs. The single place to express this is
`CommonUtility::isLiteSyncCompatible`, which already drives
`UtilityBestVfsAvailableModeJob`. `CommonUtility::fileSystemType` already recognises
`EXT234`, so only the policy and the `#else` branch change. Network filesystems
(NFS, CIFS, FUSE-mounted cloud drives) are rejected.

### D5 — Hydration transport

The FUSE handler blocks on the read of a dehydrated file until content is available. For
the POC the FUSE session runs in a dedicated thread inside the server process. `VfsLinux`
exposes a hydration callback to the server, which forwards the request to
`SyncPal::addDlDirectJob`; the existing direct-download job writes the content to the
placeholder. `VfsLinux::forceStatus` (called by `finalizeDirectDownload`) signals the
condition variable that unblocks the FUSE thread. A per-path state makes concurrent opens
single-flight, and a 10-minute timeout prevents a wedged mount. A separate helper process
is a later optimisation, not a POC requirement; it would need an IPC channel
(`src/server/comm/pipecommserver.*` / `socketcommserver.*`) and is explicitly deferred.

### D6 — Feature flag

Two layers, both required:

- Compile-time option `BUILD_LINUX_VFS` (default `OFF`) so CI and release builds are
  unaffected and the FUSE dependency stays optional.
- Runtime gate through the existing parameters cache, read by
  `isVfsPluginAvailable`/`bestAvailableVfsMode`, so a production build can ship the plugin
  disabled.

Exact parameter key is decided in phase 0 and recorded here when implemented.

### D7 — Deactivation is rehydration, not wipe

On macOS/Windows disabling Lite Sync wipes virtual files. For the POC, disabling on Linux
hydrates all remaining placeholders while the mount is still active, then stops the sync
and unmounts. `VirtualFilesCleaner` keeps hydrated placeholders on disk and only clears
their attributes, so no content is deleted. Losing the mount must never propagate remote
deletions: the deactivation path adds an explicit `hydrateAllPlaceholders()` step before
the existing `setSupportsVirtualFiles` stop/wipe sequence, and aborts the operation if any
hydration fails.

---

## 4. Target Architecture

```
+--------------------------- kDrive server process ----------------------------+
|                                                                              |
|  AppServer::tryCreateAndStartVfs  --createVfsFromPlugin("linux")-->           |
|                                                                              |
|   +--------------------- kdrive_vfs_linux (plugin) ----------------------+   |
|   |  VfsLinux : KDC::Vfs                                                  |   |
|   |    startImpl -> mount FUSE session (thread)                           |   |
|   |    createPlaceholder -> node in FUSE tree + user.* xattrs              |   |
|   |    status/isDehydratedPlaceholder -> xattr lookup                      |   |
|   |    hydrate/dehydrate -> enqueue on Vfs worker queues                   |   |
|   |    stopImpl -> flush + unmount                                         |   |
|   +-----------------------------------------------------------------------+   |
|              | mount event (open/read)                    ^ download result   |
|              v                                            |                   |
|   +---------------------- FUSE session --------------------+-------------+   |
|   |  read(placeholder) -> request hydration, block until hydrated         |   |
|   +----------------------------------------------------------------------+   |
|                                                                              |
|  SyncPal / ExecutorWorker / SyncLocalDeleteJob (unchanged, use Vfs)          |
+------------------------------------------------------------------------------+
                 |  /run/user/<uid>/kDrive/vfs-<syncDbId> (mount point)
                 v
        user applications see placeholders and transparent hydration
```

### Component responsibilities

| Component | Responsibility |
|---|---|
| `VfsLinux` | Implements `KDC::Vfs`; owns mount lifecycle, placeholder creation, metadata, pin state, hydration/dehydration queues |
| FUSE session | Presents the sync folder, resolves node paths, blocks reads of dehydrated files |
| Hydration queue | Reuses `Vfs` workers; maps a path to the server download path used by the sync engine |
| Server | Unchanged orchestration; only platform gates and availability logic are extended |
| Linux GUI | Optional entry points; no new server API required beyond `supportVfs` |

### Vfs interface contract for Linux

The plugin must implement every pure virtual of `KDC::Vfs`. POC-critical semantics:

| Method | Linux behaviour |
|---|---|
| `startImpl` / `stopImpl` | Mount / unmount the FUSE session; report `UnableToStartVfs` when `/dev/fuse` is unavailable |
| `createPlaceholder` | Create a node with size and mtime from `SyncFileItem`, marked dehydrated |
| `convertToPlaceholder` | Mark an existing local file as placeholder, keeping content if hydrated |
| `dehydratePlaceholder` | Drop content, keep metadata, update status attribute |
| `isDehydratedPlaceholder` | Read status attribute; `true` when not hydrated |
| `status` | Fill `VfsStatus` (`isPlaceholder`, `isHydrated`, `isSyncing`, `progress`) from attributes |
| `updateMetadata` | Update size/mtime/node id without touching content |
| `updateFetchStatus` / `forceStatus` | Reflect hydration progress and syncing flag |
| `hydrate` / `cancelHydrate` | Enqueue or cancel a download through the workers |
| `pinState` / `setPinState` | Map `AlwaysLocal` / `OnlineOnly` to `P` / `U` |
| `isExcluded` / `exclude` | POC: always `false` / no-op, exclusions disabled |
| `setAppExcludeList` / `getFetchingAppList` | POC: no-op (`ExitCode::Ok`) |
| `fileStatusChanged` | POC: no-op returning `true`; no file-manager integration |
| `showPinStateActions` | `false` |
| `isRegistered` | `true` while the mount is active |

---

## 5. File Inventory

### 5.1 New files

| Path | Purpose |
|---|---|
| `docs/design/litesync-linux-poc.md` | This document |
| `src/server/vfs/linux/CMakeLists.txt` | Plugin target, includes, link, install into `PLUGINDIR` |
| `src/server/vfs/linux/vfs_linux.h` | `VfsLinux` class and plugin factory declarations |
| `src/server/vfs/linux/vfs_linux.cpp` | `KDC::Vfs` implementation: mount lifecycle, placeholder creation, xattr metadata |
| `src/server/vfs/linux/fusesession.h` | FUSE session wrapper: mount, unmount, loop lifecycle |
| `src/server/vfs/linux/fusesession.cpp` | FUSE callbacks: `getattr`, `readdir`, `open`, `read`, `write`, `create`, `truncate`, `rename`, `unlink`, `setxattr`, `getxattr` |
| `src/server/vfs/linux/placeholdermetadata.h` | Attribute names and value helpers (`user.kDrive.status`, `user.kDrive.pinstate`) |
| `src/server/vfs/linux/placeholdermetadata.cpp` | Read/write/remove attributes with `IoHelper` primitives |
| `src/server/vfs/linux/nodemap.h` | Path-to-node mapping and inode allocation for the FUSE tree |
| `test/libcommonserver/vfs/testlinuxvfs.h` | Unit tests for placeholder metadata, pin state and the node map |
| `test/libcommonserver/vfs/testlinuxvfs.cpp` | Unit tests, no real mount or root privileges required |
| `litesync-exclude-linux.lst` | Placeholder exclusion list for Linux indexers (empty/comment-only in the POC) |
| `infomaniak-build-tools/linux/build-and-test-linux-vfs.sh` | Installs the Linux dependencies, builds with `BUILD_LINUX_VFS=ON` and runs the unit tests |

### 5.2 Modified files

Paths under `src/gui4/linux/` exist only on the `protected/linux-v4` branch today; the other
paths are on `develop`.

| Path | Change | Phase |
|---|---|---|
| `src/libcommon/utility/cstypes.h:326` | Add `Linux` before `EnumEnd`; keep existing integer values stable | 1 |
| `src/libcommonserver/vfs/vfs.cpp:96`, `:113` | New stable mode string (`"linux"`) in `modeToString`/`modeFromString` | 1 |
| `src/libcommonserver/vfs/vfs.cpp:265` | `modeToPluginName` returns `"linux"` | 1 |
| `src/libcommonserver/vfs/vfs.cpp:271` | Linux branch of `isVfsPluginAvailable` (plugin load + `/dev/fuse` access) | 1 |
| `src/libcommonserver/vfs/vfs.cpp:327` | Include `Linux` in `bestAvailableVfsMode()` ordering | 1 |
| `src/libcommon/utility/utility.cpp:363` | `isLiteSyncCompatible` policy for ext4/xfs/btrfs | 1 |
| `src/server/requests/serverrequests.cpp:835` | `supportVfs` uses `isLiteSyncCompatible` instead of `isNTFS || isAPFS` | 1 |
| `src/server/requests/serverrequests.cpp:843` | Linux branch: `liteSync ? VirtualFileMode::Linux : Off` | 1 |
| `src/server/migration/migrationparams.cpp:413` | Same compatibility rule for migrated syncs | 1 |
| `src/server/appserver.cpp:431` | Log Linux plugin availability at startup | 1 |
| `src/server/vfs/CMakeLists.txt:3` | Add `elseif(UNIX AND NOT APPLE) list(APPEND VIRTUAL_FILE_SYSTEM_PLUGINS "linux")` | 1 |
| `src/server/CMakeLists.txt:283` | Add the Linux plugin as a server dependency and packaging input | 1 |
| `src/server/appserver.cpp:4250` | `installVfs` handles Linux (check `/dev/fuse`, no extension install) | 2 |
| `src/libcommonserver/io/iohelper_linux.cpp:47` | Real `checkIfFileIsDehydrated` backed by placeholder metadata | 2 |
| `src/libcommonserver/io/iohelper.h:34` | Expose placeholder attribute constants on Linux | 2 |
| `src/libsyncengine/propagation/executor/executorworker.cpp:713` | Linux branch of `convertToPlaceholder` | 2 |
| `src/libsyncengine/update_detection/file_system_observer/localfilesystemobserverworker.cpp:399` | Handle `VirtualFileMode::Linux` | 2 |
| `src/libsyncengine/jobs/network/kDrive_API/downloadjob.cpp:393` | No change needed: the `isNTFS` check only guards Windows junction creation, not sparse files | — |
| `src/libsyncengine/syncpal/syncpal.cpp:757` | No direct change: `forceStatus` is already called by `finalizeDirectDownload` and now signals the Linux hydration waiter | — |
| `src/server/comm/guijobs/utilitybestvfsavailablemodejob.cpp:72` | Update the log/message: compatible filesystems are not only NTFS/APFS | 1 |
| `src/server/comm/guijobs/guijobfactory.cpp:145` | No change expected; verify the best-mode job is reachable on Linux | 1 |
| `CMakeLists.txt:271` | Install `litesync-exclude-linux.lst` for Linux packages | 4 |
| `conanfile.py` (`requirements()`) | No change: `libfuse3` comes from the system package and is discovered with `pkg-config` | 1 |
| `infomaniak-build-tools/linux/build-release-appimage.sh` | Bundle `libfuse3.so` and `fusermount3` | 4 |
| `infomaniak-build-tools/linux/build-release-via-podman.sh` | Install `libfuse3-dev`; document the missing `/dev/fuse` in containers | 4 |
| `test/libcommonserver/CMakeLists.txt` | Add the Linux VFS helper sources and tests to the `elseif(UNIX)` branch | 2 |
| `src/libcommonserver/vfs/vfs.h` | Hydration request callback and `hydrateAllPlaceholders()` default | 3/5 |
| `src/server/appserver.cpp`, `src/server/appserver.h` | Hydration callback, deactivation pre-hydration | 3/5 |
| `src/gui/drivepreferenceswidget.cpp` | Enable the Lite Sync confirmation on Linux | 6 |
| `src/gui/localfolderdialog.cpp`, `src/gui/adddrivelocalfolderwidget.cpp` | Linux filesystem compatibility check in the sync creation flow | 6 |
| `CMakeLists.txt` | Install `litesync-exclude-linux.lst` for Linux packages | 4 |
| `test/libparms/CMakeLists.txt:65` | Copy the Linux exclusion list next to the macOS one | 4 |
| `src/gui4/windows/.../Services/CommSharedEnums.cs` | Not required for the POC: the Linux value never reaches a Windows client | — |
| `src/gui4/macOS/...` (`Synchro`, `SyncList`, mappers) | Not required for the POC: the Linux value never reaches a macOS client | — |
| `src/gui4/linux/app/onboarding/onboardingsynccreationcoordinator.cpp:163` | Stop forcing `liteSync = false` | 6 |
| `src/gui4/linux/app/settings/advancedsynccreationcontroller.cpp:263` | Same | 6 |
| `src/gui4/linux/app/settings/settingssyncactivationcontroller.cpp:342` | Same | 6 |
| `src/gui4/linux/app/storage/*`, `app/settings/*.qml` | Lite Sync toggle, sync-type display, storage card | 6 |
| `src/gui4/linux/AGENTS.md:240`, `:320` | Update the statement that LiteSync is absent on Linux | 6 |

DTO mirroring on macOS and Windows is unnecessary while `bestAvailableVfsMode()` returns
`Linux` only in a Linux server process: the value never crosses platforms. It becomes
relevant only if the mode enum is ever shared between clients.

---

## 6. Phased Plan and Acceptance Criteria

### Phase 0 — Decisions and scaffolding

Work:

- Confirm the four open questions in section 9.
- Add the `BUILD_LINUX_VFS` CMake option and the runtime flag key.
- Create the plugin skeleton and the design-doc entry in `docs/`.

Acceptance criteria:

- [ ] `cmake -B build -DBUILD_LINUX_VFS=ON` configures; `OFF` produces a build identical to current Linux output.
- [ ] Design decisions D1–D7 are confirmed by the maintainers, or amended in this document.
- [ ] No change to macOS/Windows build graphs.

### Phase 1 — Plumbing (no mount)

Work:

- Enum, mode strings, plugin name mapping, availability check.
- Filesystem policy and `supportVfs`/`virtualFileMode` computation.
- Empty plugin returning `UnableToStartVfs` on Linux until phase 2.

Acceptance criteria:

- [ ] `Vfs::modeToString(VirtualFileMode::Linux)` round-trips through `modeFromString`.
- [ ] With `-DBUILD_LINUX_VFS=ON`, `isVfsPluginAvailable(VirtualFileMode::Linux, error)` returns `true` on a machine with `/dev/fuse` and the plugin installed, `false` (with a message) without them.
- [ ] `bestAvailableVfsMode()` returns `Linux` on ext4/xfs/btrfs, `Off` on FAT/exFAT/NFS/CIFS.
- [ ] Creating a sync with `liteSync: true` on ext4 stores `virtualFileMode = 3` (or the chosen stable value) and `supportVfs = true` in the `sync` table; on an incompatible filesystem it stores `Off`/`false`.
- [ ] Existing syncs created before this phase are migrated or recomputed at startup to the new compatibility rule.
- [ ] macOS and Windows DTO parsing of `virtualFileMode` still works for their own values.
- [ ] Unit tests for names, availability and filesystem policy pass; `clang-format` produces no diff.

### Phase 2 — Mount and placeholders

Status: implemented; validation on a real `/dev/fuse` mount is pending.

Work:

- FUSE session and node map.
- `createPlaceholder`, `convertToPlaceholder`, `isDehydratedPlaceholder`, `status`, `updateMetadata`.
- Linux branches in the executor and the local observer.

Acceptance criteria:

- [ ] A sync created with Lite Sync mounts at the configured mount point; `findmnt` reports it, and `ls` shows the remote tree.
- [ ] Files created remotely appear as placeholders without downloading their content (local disk usage of the sync folder stays below the sum of file sizes by at least the configured placeholders' size).
- [ ] `isDehydratedPlaceholder` returns `true` for placeholders and `false` for hydrated files.
- [ ] The sync engine does not upload placeholder content and does not report content-diff conflicts for them.
- [ ] Unmounting leaves the sync folder clean; no remote deletion is issued.
- [ ] Unit tests pass without root privileges (FUSE syscalls mocked); an integration test on ext4 is recorded manually.

### Phase 3 — Hydration on open

Status: implemented; validation against a real sync account is pending.

Work:

- Blocking `read` path, hydration queue, progress reporting, cancellation, single-flight per node.
- Hydration-complete notification replacing the macOS extension hook.

Acceptance criteria:

- [ ] `cat <placeholder>` returns the full content and the file becomes hydrated.
- [ ] Two concurrent opens of the same placeholder trigger one download; both return the same content.
- [ ] `status()` reports `isSyncing = true` with progress during hydration and `isHydrated = true` afterwards.
- [ ] Cancelling a hydration leaves a consistent placeholder (no truncated or partially hydrated file).
- [ ] A failed download leaves the placeholder dehydrated and reports an error through `ExitInfo`; the file remains retryable.
- [ ] Opening a large file (for example 1 GiB) does not read more file content from the remote beyond the requested range, or this limitation is documented if whole-file download is kept for the POC.

### Phase 4 — Edits, deletes, moves, exclusions

Status: implemented. FUSE write/create/truncate/rename/unlink are handled by the session,
the node map moves whole subtrees, and the Linux exclusion list is installed.

Work:

- `write`/`create`/`truncate`/`rename`/`unlink` handling in the FUSE session.
- `SyncLocalDeleteJob` and blacklist behaviour with `vfsMode() == Linux`.
- Exclusion list installation and `setAppExcludeList` (may remain a no-op).

Acceptance criteria:

- [ ] Editing a hydrated file propagates the new content to the server.
- [ ] Creating a file inside the mount, writing and closing it results in an upload and a later dehydration to placeholder.
- [ ] Renaming and moving placeholders inside the mount preserves content and node identity and propagates to the server.
- [ ] Deleting a placeholder sends the item to the server trash; no local data is destroyed unexpectedly.
- [ ] Blacklisted items remain consistent across restart.
- [ ] No regression in the existing sync tests with `vfsMode = Off`.

### Phase 5 — Activation and deactivation

Status: implemented. Enabling converts existing files to hydrated placeholders; disabling
hydrates all placeholders before unmounting and cleaning attributes; stale mounts are
detached at startup.

Work:

- `setSupportsVirtualFiles` Linux path: mount/unmount ordering, rehydration before wipe.
- Error surfaces for mount failures.

Acceptance criteria:

- [ ] Enabling Lite Sync on an existing VfsOff sync mounts it and converts eligible files to placeholders without data loss.
- [ ] Disabling Lite Sync unmounts first, rehydrates remaining placeholders, then leaves the sync in `VfsOff` and running.
- [ ] Killing the server while mounted leaves no stale mounts (`/run/user/<uid>/kdrive/*` cleaned on next start).
- [ ] A mount failure (missing `/dev/fuse`, permission denied) aborts activation with a clear `ExitCause` and leaves the sync unchanged and healthy.
- [ ] No remote deletions occur in any of the above scenarios (verified against the server trash and activity log).

### Phase 6 — GUI entry points

Status: legacy Qt GUI implemented. The Linux v4 QML GUI lives on a different branch and is
not part of this `develop` change; its toggles remain to be wired when that branch is
merged.

Work:

- Legacy Qt GUI: enable the existing Lite Sync menu and warning texts on Linux.
- Linux v4 QML: sync type toggle, Lite Sync status, storage card, error strings.
- Documentation update in `src/gui4/linux/AGENTS.md`.

Acceptance criteria:

- [ ] Legacy GUI shows "Activate Lite Sync" only when `bestAvailableVfsMode()` is not `Off` for the chosen folder.
- [ ] Linux v4 creates a sync with Lite Sync when the user selects it, and shows the current mode in sync settings.
- [ ] Disabling from the GUI runs the phase 5 flow.
- [ ] Strings are translated through the existing translation pipeline; no hard-coded user-facing text.
- [ ] `AGENTS.md` no longer states that Lite Sync is absent on Linux.

---

## 7. Test Strategy

| Level | Scope | Where |
|---|---|---|
| Unit | Mode strings, filesystem policy, metadata helpers, node map, pin state | `test/libcommon`, `test/libcommonserver`, `test/server/vfs/linux` |
| Component | `VfsLinux` helpers without a mount: placeholder creation, status, metadata, node map | `test/libcommonserver/vfs` |
| Sync engine | Existing `mockvfs` scenarios parameterised with `VirtualFileMode::Linux` | `test/libsyncengine` |
| Integration (manual, POC gate) | Real mount on ext4, full scenario matrix on Ubuntu 22.04 and 24.04 | Script documented in this file's appendix |
| Packaging | Plugin present in the AppImage, loads, fails gracefully without `/dev/fuse` | `infomaniak-build-tools/linux` |

Scenario matrix for the manual integration run:

1. Create sync with Lite Sync on an empty remote folder.
2. Remote file appears as placeholder; open, hydrate, close.
3. Local edit of a hydrated file.
4. Local create + close + dehydrate.
5. Rename and move placeholder.
6. Delete placeholder; restore from server trash.
7. Pause/resume sync while mounted.
8. Disable Lite Sync; verify rehydration and no remote deletion.
9. Restart server while mounted; verify clean remount.
10. Mount failure without `/dev/fuse`; verify unchanged sync.

CI note: containers and GitHub runners have no `/dev/fuse`. Automated CI covers unit and
component tests only; the integration run is manual and recorded in the POC report.

---

## 8. Build, Packaging and Dependencies

- `BUILD_LINUX_VFS` defaults to `OFF`; the release pipeline keeps producing the current
  binary until the POC is accepted.
- `libfuse3` is the only new native dependency. It is installed as the `libfuse3-dev` system
  package (there is no Conan recipe for it in this project) and discovered with
  `pkg-config` in `src/server/vfs/linux/CMakeLists.txt`; `build-and-test-linux-vfs.sh`
  installs it.
- The AppImage must contain `libfuse3.so*` and `fusermount3`; the setuid helper must stay
  usable inside the AppImage (document the AppImage/FUSE constraint in
  `infomaniak-build-tools/linux/Readme.md`).
- The plugin is installed into `PLUGINDIR`
  (`${CMAKE_INSTALL_FULL_LIBDIR}/kdrive/plugins`) so `addLibraryPath` in
  `src/server/appserver.cpp:424` finds it without changes. The release pipeline keeps
  `BUILD_LINUX_VFS=OFF`; packaging the plugin and `libfuse3.so` remains a POC packaging
  step because the AppImage builder would also need the matching `fusermount3` helper and
  its setuid/AppImage constraints.
- Packaging must remain functional when the plugin is absent or `/dev/fuse` is missing:
  the client starts, Lite Sync reports `Off`, sync continues.

### Local build and test script

`infomaniak-build-tools/linux/build-and-test-linux-vfs.sh` reproduces the phase 1–5
validation locally on Ubuntu 22.04/24.04:

```bash
./infomaniak-build-tools/linux/build-and-test-linux-vfs.sh
./infomaniak-build-tools/linux/build-and-test-linux-vfs.sh -t Release -j 8
```

Prerequisites:

- root or `sudo`, for the `apt-get` packages;
- a Qt account already used once with the Qt online installer, so that
  `~/.local/share/Qt/qtaccount.ini` exists, or `QT_INSTALLER_JWT_TOKEN`, or
  `QT_ACCOUNT_EMAIL`/`QT_ACCOUNT_PASSWORD` exported locally.

The script installs the system packages required by the Conan recipes
(`libglib2.0-dev`, `libsecret-1-dev`, `libc-ares-dev`, `libcurl4-openssl-dev`,
`libmount-dev`, CPPUnit), builds libzip 1.10.1 into `/usr/local` because Ubuntu 24.04
ships 1.7.3, creates the CPPUnit symlink expected by the test CMake files, installs the
Conan dependencies, configures with `-DBUILD_UNIT_TESTS=ON -DBUILD_LINUX_VFS=ON`, builds
`kDrive_vfs_linux` and the test binaries, and runs `kDrive_test_common` and
`kDrive_test_common_server` through `infomaniak-build-tools/run-test.sh`.

The script does not replace CI: the self-hosted Linux runner remains the reference build,
and a container or GitHub-hosted runner has no `/dev/fuse`, so the mount integration stays
manual (section 10).

---

## 9. Risks and Open Questions

### Risks

| Risk | Impact | Mitigation |
|---|---|---|
| FUSE changes inode/stat semantics seen by the local observer | Duplicate detections, spurious conflicts | Mount point separate from the observed real folder, or observer ignores the mount; covered by phase 2 criteria |
| Server observes its own mount | Event loops, infinite re-sync | Explicit exclusion of the mount root from local observation; assert in logs |
| Lost mount or crashed daemon | Remote deletions, local data loss | Unmount-before-anything ordering, rehydration on deactivation, deletion guards already present for Windows sync-root loss |
| `/dev/fuse` permissions and container builds | Feature unavailable, CI blind spot | Runtime availability check, graceful `Off`, manual integration gate |
| Extended attributes unsupported or unstable under FUSE | Metadata loss | Fallback node map owned by the plugin (D3) |
| Sparse-file assumptions | Corrupted content or wasted space | Generalise the `isNTFS` check; test 1 GiB file |
| Broken DTO parsing for new enum value | macOS/Windows GUI regressions | Append-only enum, default `Off` for unknown values, DTO tests |
| AppImage/FUSE interaction | Feature dead in the shipped package | Packaging test in phase 4, documented constraint |

### Open questions

1. Is the POC goal technical feasibility (hidden flag) or a user-facing demo on Linux v4?
2. Confirm D1: FUSE only, or also evaluate a GIO/gvfs extension as an alternative?
3. Minimum filesystem set: ext4 only, or ext4 + xfs + btrfs from the start?
4. Are application exclusions required for the POC, or is `setAppExcludeList` a no-op acceptable?
5. Is Flatpak/Snap support a future requirement? It affects the mount strategy (portal, `--device=all`).

---

## 10. Appendix — Manual Integration Script (POC gate)

Prerequisites:

- Ubuntu 22.04 or 24.04, user in the `fuse` group or `fusermount3` available, `/dev/fuse` present.
- Build with `-DBUILD_LINUX_VFS=ON`.
- A test account and an empty remote folder.

Steps:

1. Create a sync on an ext4 path with Lite Sync enabled; confirm the mount in `findmnt`.
2. Upload a 10 MiB file from another client; confirm it appears as a placeholder without
   full local allocation (`du` before/after).
3. `cat` the placeholder; confirm content and `status` transitions (syncing → hydrated).
4. Modify and save the file; confirm the server receives the new content.
5. Dehydrate via the plugin entry point; confirm the file returns to placeholder.
6. Rename and move the placeholder; confirm propagation.
7. Delete the placeholder; confirm server trash entry.
8. Disable Lite Sync; confirm unmount, rehydration and that the server trash contains no
   unexpected deletions.
9. Restart the client while mounted; confirm clean remount and no stale mount directory.
10. Remove `/dev/fuse` access temporarily; confirm activation fails cleanly and the sync
    still works in `VfsOff`.

Record for each step: client log excerpt, `findmnt` output, `getfattr -d` of a placeholder,
server activity entry.
