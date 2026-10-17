import {getFileNameFromHeader} from "~/utils/ExtractFileNameFromHeader";
import {getContentDisposition, getContentType} from "~/utils/HeaderUtils";
import mimeDb from "mime-db";

export function getFileExtension(name: string): string {
    return name
        .split(".")
        .pop() ?? name
}

export function getFileNameWithoutExtension(name: string): string {
    if (!name) return name;
    const dotIndex = name.lastIndexOf('.');
    // ignore a leading dot (e.g. ".env") by requiring dotIndex > 0
    return dotIndex > 0 ? name.slice(0, dotIndex) : name;
}

export function getFileFromUrl(url: string): string | null {
    try {
        return new URL(url, "http://dummy.base")
            .pathname.split("/").pop() ?? null
    } catch (error) {
        return null;
    }
}

export function getFileFromHeaders(responseHeaders: Headers) {
    const value = getContentDisposition(responseHeaders)
    if (value == null) return null
    return getFileNameFromHeader(value)
}

export function isRegisteredDownloadContent(
    url: string,
    responseHeaders: Headers,
    registeredFileTypes: readonly string[],
): boolean {
    const fileName = getFileFromHeaders(responseHeaders) ?? getFileFromUrl(url)
    if (fileName !== null && fileName.lastIndexOf(".") > 0) {
        return registeredFileTypes.includes(getFileExtension(fileName).toLowerCase())
    }
    // MIME inference is only safe for explicit downloads, not inline page/media resources.
    const disposition = getContentDisposition(responseHeaders)?.split(";", 1)[0].trim().toLowerCase()
    if (disposition !== "attachment") {
        return false
    }
    const contentType = getContentType(responseHeaders)?.split(";", 1)[0].trim().toLowerCase()
    const extensions = contentType ? mimeDb[contentType]?.extensions : undefined
    return extensions?.some(extension => registeredFileTypes.includes(extension)) ?? false
}
