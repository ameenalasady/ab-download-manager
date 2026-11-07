# Toolkit-neutral download facade and lifecycle policy

The original `DownloadSystem`, `DownloadFoldersRegistry`, extra-settings **contracts**,
error reason/registry/in-memory failure tracking, and download/queue completion
contracts/runners live here under their original package names (extracted from the
removed Compose app's `shared:app`). Extra-settings storage, platform observers and
action providers are supplied by the service. This is not a second engine, repository or scheduler.

`DownloadEnginePolicy` captures the original `BaseAppRepository` engine subset.
Its controller applies core settings/reload/global limiter, manual-request
concurrency and monitor average-speed policy. The service uses it synchronously
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
functions. The service loads the original English fallback strings from its own
resources. It currently uses fallback errors, not full specialized/localized error parity.

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
All service listeners join before the existing transfer/store/lease drain.
Regression tests are in `desktop/service/.../DownloadLifecycleTest.kt`; real-engine
failure/restart/retry/checksum and Qt workflow coverage are in the service smoke.
