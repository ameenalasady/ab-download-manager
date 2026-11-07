# Shared category domain

Extracted existing category implementation, with original package names and JSON
serializers, for the Kotlin service. Depends on existing core storage/manager and
utility/coroutine libraries, not a second repository or engine.

`StandardCategoryDefaults` holds the original IDs/order/filter lists, accepting
frontend-provided icon URIs and a default-folder provider. CategoryManager accepts the
neutral `CategoryDefaults` interface.

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
