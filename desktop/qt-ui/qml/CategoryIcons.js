// Category icon URIs exactly as the original app stores them, in its icon picker order.
// The original's URI names are historical and do not match the drawn glyph (for example
// "icon:pictureFile" is the archive icon used by Compressed); `icon` is the matching
// desktop theme icon. Unknown URIs keep their stored value and show a generic file.
.pragma library

var choices = [
    {uri: "icon:fileOpen", label: "Pictures", icon: "image-x-generic"},
    {uri: "icon:folderOpen", label: "Music", icon: "audio-x-generic"},
    {uri: "icon:pictureFile", label: "Compressed", icon: "package-x-generic"},
    {uri: "icon:musicFile", label: "Videos", icon: "video-x-generic"},
    {uri: "icon:zipFile", label: "Programs", icon: "application-x-executable"},
    {uri: "icon:videoFile", label: "Documents", icon: "text-x-generic"},
    {uri: "icon:applicationFile", label: "Other", icon: "application-octet-stream"},
    {uri: "icon:file", label: "File", icon: "document-new"},
    {uri: "icon:folder", label: "Folder", icon: "folder"},
    {uri: "icon:browserIntegration", label: "Web", icon: "globe"},
    {uri: "icon:appearance", label: "Appearance", icon: "preferences-desktop-color"},
    {uri: "icon:settings", label: "Settings", icon: "configure"},
    {uri: "icon:search", label: "Search", icon: "search"},
    {uri: "icon:info", label: "Information", icon: "help-about"},
    {uri: "icon:check", label: "Check", icon: "dialog-ok"},
    {uri: "icon:link", label: "Link", icon: "link"},
    {uri: "icon:download", label: "Download", icon: "download"},
    {uri: "icon:speaker", label: "Speaker", icon: "audio-volume-high"},
    {uri: "icon:group", label: "Group", icon: "group"},
    {uri: "icon:activeCount", label: "List", icon: "view-list-details"},
    {uri: "icon:speed", label: "Speed", icon: "speedometer"},
    {uri: "icon:resume", label: "Resume", icon: "media-playback-start"},
    {uri: "icon:pause", label: "Pause", icon: "media-playback-pause"},
    {uri: "icon:stop", label: "Stop", icon: "media-playback-stop"},
    {uri: "icon:queue", label: "Queue", icon: "view-media-playlist"},
    {uri: "icon:remove", label: "Remove", icon: "edit-delete"},
    {uri: "icon:clear", label: "Clear", icon: "edit-clear"},
    {uri: "icon:add", label: "Add", icon: "list-add"},
    {uri: "icon:paste", label: "Paste", icon: "edit-paste"},
    {uri: "icon:copy", label: "Copy", icon: "edit-copy"},
    {uri: "icon:refresh", label: "Refresh", icon: "view-refresh"},
    {uri: "icon:share", label: "Share", icon: "document-share"},
    {uri: "icon:lock", label: "Lock", icon: "object-locked"},
    {uri: "icon:question", label: "Question", icon: "question"},
    {uri: "icon:verticalDirection", label: "Sort", icon: "view-sort"},
    {uri: "icon:downloadEngine", label: "Download Engine", icon: "speedometer"},
    {uri: "icon:network", label: "Network", icon: "network-wired"},
    {uri: "icon:externalLink", label: "External Link", icon: "document-open-remote"}
]

function indexOf(uri) {
    for (var i = 0; i < choices.length; ++i) if (choices[i].uri === uri) return i
    return -1
}

function themeIcon(uri) {
    var index = indexOf(uri)
    return index >= 0 ? choices[index].icon : "text-x-generic"
}
