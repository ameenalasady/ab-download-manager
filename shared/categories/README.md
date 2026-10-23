# Shared category domain

Extracted existing category implementation, with original package names and JSON
serializers, for Compose and the isolated Kotlin service. Depends on existing core
storage/manager and utility/coroutine libraries, not a second repository or engine.
Compose `@Immutable` stability annotations are compile-only; this module does not
require a Compose runtime. Icon rendering/helpers remain in `shared/app`.

`StandardCategoryDefaults` holds the original IDs/order/filter lists, accepting
frontend-provided icon URIs and a default-folder provider. The existing Compose
`DefaultCategories` adapter keeps its old constructor and methods. CategoryManager
accepts the neutral `CategoryDefaults` interface. DI callers must explicitly obtain
the existing `DefaultCategories` registration (not implicitly request the interface).

Existing UI behavior remains sampled autosave on the caller's scope. New service
callers serialize access on an owner dispatcher, call `persistNow()` before command
acknowledgements, then `closeAndJoin()` before cancelling that scope/releasing the
profile lease. The latter joins sampled saves and performs a final synchronous
save only after successful initialization; failed pre-boot cleanup cannot overwrite
an unreadable input file. The isolated service strictly validates storage before
boot because the original forgiving CategoryFileStorage reader can return empty
for unreadable data. It also restricts category paths; this is not production path
policy or a profile converter.

On disk, categories remain a JSON list of original records: numeric `id/items`,
name, icon URI, path/usePath, acceptedFileTypes/acceptedUrlPatterns. Category
matching, specificity, assignment and order come from the existing CategoryManager.
Changes to filters do not retroactively recategorize history. Tests exercising
legacy format, persistence, matching and joined shutdown are in
`desktop/service/src/test/kotlin/com/abdownloadmanager/service/CategoryRepositoryTest.kt`.
