# Toolkit-neutral download facade and lifecycle policy

The original `DownloadSystem`, `DownloadFoldersRegistry`, extra-settings **contracts**,
error reason/registry/in-memory failure tracking, and download/queue completion
contracts/runners now live here under their original package names. `shared:app`
exports this module, so desktop/Android DI and Compose callers keep the original
facade constructor and imports. Existing extra-settings storage implementations,
localized specialized error mappers, platform observers and action providers remain
frontend adapters. This is not a second engine, repository or scheduler.

`DownloadEnginePolicy` captures the original `BaseAppRepository` engine subset.
Its controller applies core settings/reload/global limiter, manual-request
concurrency and monitor average-speed policy. Compose uses the shared controller
through one coalesced 500 ms debounced binding; the service uses it synchronously
after its original-format settings adapter persists a validated patch. The binding
has `closeAndJoin()`. Minimum part size now also applies at boot, and Disabled speed
policy no longer initially installs the stored positive limit before the first
subscription callback. Activity observation, autostart, folder changes, deleted-file
tracking, appearance and integrations remain outside this engine-only policy.
Incomplete extension is still a core boot-only selection, not a new live rename API.

Failure reasons keep their original **memory-only** lifetime and original clearing
rules (starting/changing/completing/removing clears a reason; ordinary cancellation
is not a failure). Registry unwrapping of `TooManyErrorException` is shared, not
reimplemented by Qt. The extracted fallback mapper accepts message-rendering
functions. Compose retains current-locale resources/specialized mappers; the fixture
service loads the original English properties without Compose's resource runtime.
It currently uses fallback errors, not full specialized/localized error parity.

Listeners subscribe before start/boot returns. Failure tracking and both completion
runners expose joined close. Completion cancellation is propagated, and runners keep
canceled/replaced action references until their finalizers drain. Lifecycle callers
must serialize start/stop/close on their owner context; no concurrent restart while
closing is supported. `DownloadSystem.closeListenersAndJoin()` joins listeners/actions
**only**, not transfer/queue/category/store writers. Source desktop close drains this
and the new repository engine-policy binding; full old-frontend shutdown remains
separate work.

The core also provides `forgetDownloadKeepingFiles`: a separate metadata-only,
joined-drain path that the service uses for "Remove from list — keep files". It keeps
every payload, including incomplete-extension files. Removal with file deletion uses
the original `DownloadSystem.removeDownload(alsoRemoveFile = true)`. Queue/category
membership is detached and persisted first; the stores are not one transaction, so
a failure part-way is reported to the frontend rather than retried.

The isolated service wires the existing components directly into its validated
composition. It does not yet instantiate the complete facade/production repository,
which also requires platform/extra-settings policy. Download and queue completion
providers are explicitly no-op: no notifications, opening files, or power actions.
All service listeners join before the existing transfer/store/lease drain. The
compile-only `@Immutable` annotation does not add Compose runtime to the service.
Regression tests are in `desktop/service/.../DownloadLifecycleTest.kt`; real-engine
failure/restart/retry/checksum and Qt workflow coverage are in the service smoke.
