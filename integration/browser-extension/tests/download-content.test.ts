import assert from "node:assert/strict";
import {test} from "node:test";
import {isRegisteredDownloadContent} from "../src/utils/URLUtils";

const registered = ["mp4", "mp3", "zip", "mkv"];
const url = "https://files.example/download/7036862b/2025-11-22%20Recording";

function accepts(disposition: string | null, contentType: string, link = url, types = registered) {
    const headers = new Headers({"content-type": contentType});
    if (disposition !== null) headers.set("content-disposition", disposition);
    return isRegisteredDownloadContent(link, headers, types);
}

test("captures an extensionless RFC 5987 attachment using its registered MIME type", () => {
    assert.equal(accepts("attachment; filename*=UTF-8''2025-11-22%20Recording", "video/mp4"), true);
});

test("captures an extensionless attachment with an ASCII filename", () => {
    assert.equal(accepts('attachment; filename="Recording"', "video/mp4"), true);
});

test("uses the URL filename when an attachment does not supply one", () => {
    assert.equal(accepts("attachment", "application/zip"), true);
});

test("normalizes MIME type parameters and disposition case", () => {
    assert.equal(accepts(' Attachment ; filename="Recording"', "VIDEO/MP4; charset=binary"), true);
});

test("matches any registered extension for a MIME type, not just its canonical extension", () => {
    assert.equal(accepts('attachment; filename="Recording"', "audio/mpeg"), true);
});

test("does not capture extensionless inline media or ordinary web responses", () => {
    assert.equal(accepts('inline; filename="Recording"', "video/mp4"), false);
    assert.equal(accepts(null, "video/mp4"), false);
    assert.equal(accepts('attachment-preview; filename="Recording"', "video/mp4"), false);
    assert.equal(accepts('attachment; filename="Recording"', "text/html"), false);
});

test("keeps MIME types excluded by the registered file types excluded", () => {
    assert.equal(accepts('attachment; filename="Recording"', "video/mp4", url, ["zip"]), false);
    assert.equal(accepts('attachment; filename="Recording"', "video/mp4", url, []), false);
});

test("does not infer an extension from an unknown or generic binary MIME type", () => {
    assert.equal(accepts('attachment; filename="Recording"', "application/octet-stream"), false);
    assert.equal(accepts('attachment; filename="Recording"', "application/x-unknown-format"), false);
    assert.equal(accepts('attachment; filename="Recording"', ""), false);
});

test("a filename suffix takes precedence over MIME inference", () => {
    assert.equal(accepts('attachment; filename="Recording.txt"', "video/mp4"), false);
    assert.equal(accepts('attachment; filename="Recording.MP4"', "application/octet-stream"), true);
    assert.equal(accepts('attachment; filename="Recording"', "video/mp4", "https://files.example/unused.zip", ["zip"]), false);
});

test("preserves capture of registered URL suffixes without an attachment header", () => {
    assert.equal(accepts(null, "application/octet-stream", "https://files.example/Recording.MKV?token=abc"), true);
});
