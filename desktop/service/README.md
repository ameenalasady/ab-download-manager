# Toolkit-neutral Kotlin service

`desktop/service` runs the existing AB Download Manager engine without a UI toolkit and
serves it to the Qt frontend ([`../qt-ui/`](../qt-ui/README.md)) over a private,
authenticated local socket. It has **no Compose/AWT/Swing/Koin runtime dependencies**.
It owns the existing `downloader:core` `DownloadManager`, HTTP downloader and
transactional list/part stores, and composes the original `DownloadSystem` facade,
queue manager and scheduler, category manager (`shared:categories`), download
lifecycle/failure/completion components (`shared:download-lifecycle`), the
`downloader:monitor` speed/ETA monitor and the original extra-settings stores. It does
not implement a second downloader or scheduler, and it reads and writes the original
on-disk formats, preserving unknown JSON fields.

`ServiceLifecycle` acquires the profile lease (`app-data/config/app.lock`, shared with
the Compose app's `SingleAppInstanceLocker`) **before constructing stores**. Shutdown
drains IPC commands, stops transfers, joins writer coroutines and saves state before
releasing the lease. A competing owner fails before opening persistence; lock files are
never unlinked. Read-only storage preflight rejects ambiguous/corrupt records, ID
high-water marks, parts and interrupted transactions before any writer starts; there is
no automatic repair.

## Modes

The entry point (`IsolatedService.kt`) takes exactly one of:

| Argument | Purpose |
|---|---|
| `--isolated-profile <dir>` | Test fixture mode: a profile under `build/qt-service/`, loopback HTTP fixtures only, no desktop side effects. |
| `--runtime-policy <policy.json>` | Explicit runtime for a **newly initialized** profile: HTTP(S), approved destination roots, explicit proxies, desktop integration. Created and launched by `scripts/qt-service/runtime.py`. |
| `--fixture-profile-copy <request.json>` / `--fixture-profile-verify <workspace>` | Offline, fixture-only profile copies with byte/hash inventories. They produce an unvalidated, non-runnable candidate; this is groundwork for import, not a migration. |

## Build, run, test (isolated fixture mode)

From the repository root, with JDK 25 as `JAVA_HOME`:

```sh
./gradlew :desktop:service:test :desktop:service:installDist :desktop:app:compileKotlin

# Terminal 1. A NEW profile under this exact ignored fixture subtree.
desktop/service/build/install/service/bin/service \
  --isolated-profile "$PWD/build/qt-service/manual-test"

# Terminal 2. Same Qt Wayland host, but real snapshots, not PreviewDownloads.
./scripts/qt-capability/build.sh
python3 scripts/qt-capability/run.py --snapshot-desktop-style -- \
  --view downloads --instance-tag isolated-engine \
  --service-descriptor "$PWD/build/qt-service/manual-test/qt-service.json" \
  --width 1054 --height 640

# Automated fixture server, real engine, restart, checksums, Qt adapter + QML.
PYTHONDONTWRITEBYTECODE=1 python3 scripts/qt-service/smoke.py
# Same real-engine coverage; QML forms on native Wayland in private HOME/XDG state:
PYTHONDONTWRITEBYTECODE=1 python3 scripts/qt-service/smoke.py --native-qml
```

Closing Qt does not stop the service/transfers. Stop the service with Ctrl+C or
SIGTERM; its hook drains/saves and removes its descriptor/socket. SIGKILL cannot
run that cleanup. A later service owner publishes fresh credentials/epoch; old
private temp sockets can remain after a crash. Do not indiscriminately delete
runtime directories or lock files. All fixture state stays local and ignored.

The launcher **refuses profiles outside `build/qt-service/`**, symlink aliases or
symlinks inside a profile, integration markers, stored destinations outside its
own `Downloads/`, and stored nonfixture URLs. Only HTTP `127.0.0.1` URLs with an
explicit port are accepted; the production integration port is forbidden.
Redirects and proxies are disabled in this isolated composition. This is an
application-level safety policy, **not an OS sandbox**; only use an owned local
fixture server, not arbitrary services on localhost. No production-profile,
installer, browser, autostart, tray, power action or update path is enabled in this mode.

## Explicit runtime

Source-only, disposable validation (create a NEW profile; destination must exist):

```sh
mkdir -p "$PWD/build/qt-service/runtime-example-downloads"
python3 scripts/qt-service/runtime.py init \
  --profile "$PWD/build/qt-service/runtime-example-profile" \
  --destination "$PWD/build/qt-service/runtime-example-downloads"
python3 scripts/qt-service/runtime.py launch \
  --runtime-policy "$PWD/build/qt-service/runtime-example-profile/qt-runtime-policy.json"
# Qt close deliberately leaves the service running. Explicitly stop and wait for lease release:
python3 scripts/qt-service/runtime.py stop \
  --runtime-policy "$PWD/build/qt-service/runtime-example-profile/qt-runtime-policy.json"

PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s scripts/qt-service -p 'test_*.py' -v
PYTHONDONTWRITEBYTECODE=1 python3 scripts/qt-service/runtime_smoke.py --native-ui
# NEW output directory only; local artifact, not installation:
python3 scripts/qt-service/package.py --output "$PWD/build/qt-packages/new-private-preview"
PYTHONDONTWRITEBYTECODE=1 python3 scripts/qt-service/runtime_smoke.py \
  --native-ui --package "$PWD/build/qt-packages/new-private-preview"
```

The generated version-1 private JSON explicitly contains `profile`, matching
`profileId`, `destination`, `destinationRoots`, `redirects` and `proxy`. Proxy is
`{"type":"direct"}` or explicit `http`/`socks` with `host`, numeric `port` and
optional HTTP `username`/`password`. An optional `hostProxies` object maps at most
32 exact canonical lower-case hosts (no suffix/wildcard matching) to these same
explicit policies. Redirects crossing
proxy routes reject, never use the wrong proxy or fall back to direct. System/PAC
and authenticated SOCKS remain unsupported. Only change a policy while its service
is stopped; attach never live-reloads it.
No certificate bypass exists. Owned HTTP/proxy/redirect and trusted HTTPS fixtures
pass; an independent untrusted TLS certificate rejects before payload creation.
Only a disposable per-fixture truststore is used, never the system truststore.
This is not a full HTTPS deployment matrix. Every URL/redirect excludes port 15151;
HTTPS downgrades reject and cross-origin redirects strip custom headers/secrets.
Approved roots are application policy, not a sandbox against hostile same-UID races.
Original credentials are plaintext on disk; keep policy/profile/logs private.

`download.http-options` extends Add with optional `folder`, nullable numeric
`connections`, string `speedLimit` and `credentials:{username,password,userAgent,
headers}`. `download.edit` takes stable string `id` and URL/options, requires an
inactive non-completed download and stopped containing queues, never renames/moves/
resets or automatically resumes. Omit credentials to preserve, send an empty object
to explicitly clear. Details expose only `hasCredentials`, connections and string
speedLimit, never the stored secrets. Failed edit persistence reports
`edit-partially-applied` with `id`. `runtime.policy` advertises the snapshot runtime
mode/default destination/roots/redirect/proxy kind, not proxy credentials.
`service.shutdown` accepts through the normal correlated authenticated protocol;
its reply acknowledges the request, not completed shutdown. The launcher waits for
lease release as well as descriptor removal. No automatic command replay.

In explicit runtime the service also runs the desktop integrations:

- **Browser integration:** the original browser-extension HTTP API (`BrowserBridge`)
  and the native-messaging host (`runtime.py native-host`) feed a bounded,
  secret-free intake. Requests wait in a restart-persistent review journal
  (`BrowserRequestJournal`) until the frontend accepts or dismisses them; captured
  cookies/headers stay service-side. A request whose Add outcome is unknown is never
  replayed and cannot be accepted again.
- **Notifications** use freedesktop notifications with optional custom WAV sounds.
  **Activity-dependent speed limits** use a read-only Wayland idle helper.
- **Queue power actions** (Shut Down/Sleep/Hibernate on queue completion or stop
  time) only create a pending request. The frontend shows a 30 s countdown and
  answers each request once with `power.execute` or `power.cancel`; execution drains
  downloads first, then uses logind/systemctl. The service never acts on its own.

Run the notification test, which needs a private bus, with:

```sh
dbus-run-session -- env ABDM_TEST_PRIVATE_BUS=1 ./gradlew --no-daemon :desktop:service:test
```

## Settings and queue ownership

`EngineSettingsRepository` reads the existing `app-data/config/appSettings.json`
shape, retaining unknown/UI/integration fields. Supported writes: thread count,
manual-request concurrency (0 = unlimited), retry count, minimum part size,
dynamic parts, sparse allocation, server last-modified time, fixed Enabled/Disabled
global speed-limit policy/value, the existing monitor's average-speed policy, display units,
default folder (inside the runtime roots), duplicate handling, sorting and notification sounds. Complete patches
validate before transactional writes; disk uses the original numeric byte values,
IPC uses strings. Saves are synchronous under the command gate, not an unowned
background preferences writer. Core defaults match the original desktop defaults.
The boot-only incomplete-extension setting is respected when stored. Fixed speed
limits apply to the core limiter on boot/update. Stored activity-dependent modes
boot with the original unsupported fallback; a configured activity adapter also
allows selecting that mode and applies input/background changes on the owner thread.
Reading integration/autostart keys alone never starts those integrations. This is subset format compatibility, not full profile parity.

Manual Resume/Download now uses the existing manual queue, including its concurrency
limit. Named queues retain their own concurrency limits, as in the original facade;
the manual limit is not a new aggregate cap across all named queues. A core fix
removes paused *pending* work before stop (which may emit no cancellation event),
and deduplicates repeated manual resumes. Moving/removing active queue membership
requires pausing first, avoiding untracked transfers outside queue accounting.

Queue command/configuration/snapshot access and existing queue coroutines share one
service owner dispatcher. Core queue constructors accept an optional owner context;
the Compose frontend's defaults stay source-compatible. `closeAndJoin` joins queue
scheduler/listener/autosaver children before final persistence. IPC drains first,
then queue producers join, transfers/engine writers join/save, completed membership
is reconciled and queues are saved again, then the dispatcher and lease close.
Queue deletion also joins before deleting its file, so autosave cannot recreate it.
The monitor's subscription/list/speed-meter jobs and category sampled autosaver also
cancel and join before engine writers/lease close; category final save is synchronous.
A failed drain/save retains the lease. Unit tests check no late JSON writes after
lease reacquisition, including enabled schedules/categories, and fail-closed corrupt
queue/category boot.

## Private local protocol v1

- AF_UNIX socket in a fresh mode-0700 directory; socket mode 0600. Private profile
  descriptor `qt-service.json` is mode 0600 and atomically replaced while locked.
  It contains version, service epoch, endpoint and a fresh random 256-bit bearer
  token. Never commit, upload, print or put that credential on a command line.
  Authentication protects against other users, not a compromised same-UID process.
- Each frame is a 4-byte unsigned big-endian byte length then strict UTF-8 JSON.
  Requests/replies are bounded at 2 MiB. Zero/oversized/truncated/malformed frames
  are rejected. At most four clients, no unbounded worker queue; a stalled reader
  or writer is closed after ten seconds. Commands serialize at the engine gate.
- First frame: `{type:"hello",version:1,token:...,clientId:...}`. Response negotiates
  version, epoch, explicit supported commands, frame bound and deduplication window.
  Bad credentials/version terminate the connection without details.
- `{type:"snapshot"}` returns a complete immutable snapshot with revision, explicit
  ready state and stable string IDs. Sizes/byte counts/timestamps are decimal
  strings, avoiding JavaScript's 53-bit rounding. Snapshots also carry the engine
  settings subset, manual pending IDs, persisted categories/filters/membership and
  named queues/config/items/schedules. `monitor.stats` advertises measured row
  `speed` (bytes/sec) and `remainingTime` (seconds), decimal strings; -1 means
  unreported. Estimates come from the original monitor, not frontend counters.
  Revisions change with any exposed snapshot state.
  Histories exceeding the frame bound return `serviceError/snapshot-too-large`,
  not a partial/truncated history (see `history.page` below).
- Commands carry `type:"command"`, the current `epoch`, a unique `commandId`, `op` and
  arguments. Replies correlate `commandId` and report success or an explicit `code`
  plus bounded error text. Mutations that cross stores which are not one transaction
  report `*-partially-applied` with the affected `id` and an inspect-before-retry
  warning; nothing is replayed.
- A bounded 1,024-entry `(clientId,commandId)` result cache makes identical retries
  idempotent across connections **within this service epoch/window**. Reusing an
  ID for a different request or supplying a stale epoch closes the connection.
  This is **not durable exactly-once delivery** across service restart/eviction.
- Qt requests snapshots at 250 ms with one outstanding request. Pulling coalesces
  intermediate engine states and bounds per-client backpressure. Qt reconnects,
  authenticates and resnapshots; it never automatically replays commands. A lost
  command reply preserves an **outcome unknown** warning until explicitly cleared.
  Cached rows remain visibly disconnected with actions disabled.

### Capabilities

`hello` lists the supported commands (`EngineSession.capabilities`); the frontend hides
anything not advertised. Groups:

| Area | Commands |
|---|---|
| Downloads | `add` (optional `start`, `categoryId`, `duplicatePolicy`), `pause`, `resume`, `download.forget` (`id`, `keepFiles`; `false` deletes the files), `download.delete`, `download.replace`, `download.reset`, `download.move`, `download.edit`, `download.http-options`, `download.links`, `download.stopAll` |
| Details | `download.details` (source, folder, bounded original error reasons), `download.parts` (read-only `partsFor` selection: ≤16 IDs, ≤256 engine ranges each), `download.windows`, `download.checksum` (MD5/SHA-1/SHA-256/SHA-512, one bounded worker, original `fileChecksum` storage), `monitor.stats` |
| Review | `download.probe`, `download.review`, `links.review`, `links.import`, `browser.integration`, `browser.intake`, `browser.add`, `browser.accept`, `browser.dismiss`, `browser.probe`, `browser.probeBatch` |
| Queues | `queue.create/update/schedule/add/remove/move/start/stop/delete`, `queue.startAll`, `queue.stopAll`, `queue.powerAction`, `power.execute`, `power.cancel` |
| Categories | `category.create/update/delete/move/assign`, `category.icon`, `category.destination` (explicit runtime; existing writable folders inside the roots) |
| Settings | `settings.update`, `settings.speed`, `settings.display`, `settings.policy`, `settings.integration`, `settings.folder`, `settings.duplicates`, `settings.sort`, `settings.notificationSounds`, `browser.settings` |
| History | `history.page`, `history.delta`: bounded summaries (1–200 rows, 256 KiB) with cursors bound to order/epoch |
| Runtime | `snapshot`, `runtime.policy`, `system.activity`, `frontend.activate`, `service.shutdown` |

## Limitations

- The Qt frontend still consumes full snapshots, so very large histories exceed the
  2 MiB frame bound; `history.page`/`history.delta` exist but are not yet used.
- No import or activation of an existing Compose profile; explicit runtime starts from a
  new profile.
- HLS, system/PAC proxies and authenticated SOCKS are not implemented.
- The command result cache is not durable exactly-once delivery across restarts.
- The full Compose `MainApplication`/`BaseAppRepository` policy graph is not composed;
  the service uses the original English error wording, not the localized mappers.
- Approved roots are an application policy, not a sandbox against hostile same-UID
  processes.
