
<a href="https://addons.mozilla.org/en-US/firefox/addon/ab-download-manager/"><img src="https://img.shields.io/amo/v/ab-download-manager?label=Firefox&logo=firefoxbrowser" alt="Firefox Add-ons Version"></a>
<a href="https://chromewebstore.google.com/detail/bbobopahenonfdgjgaleledndnnfhooj"> <img src="https://img.shields.io/chrome-web-store/v/bbobopahenonfdgjgaleledndnnfhooj?label=Chrome&logo=googlechrome" alt="Chrome Web Store Version"></a>

# AB Download Manager Browser Integration Repository

> If you are looking for the main app repository, use [this link](https://github.com/amir1376/ab-download-manager).

## Local extensionless-download fix

This directory vendors the browser integration at upstream revision
[`658a0c70188d78033f478ba51c102a273322166f`](https://github.com/amir1376/ab-download-manager-browser-integration/commit/658a0c70188d78033f478ba51c102a273322166f)
(version 1.5.0), with the original Apache-2.0 license retained.
It is a subproject of the Plasma checkout, not a separate Git repository.

The local patch captures extensionless `Content-Disposition: attachment` responses
when their MIME type maps to a registered file extension. Both the web-request
interceptor and browser-download fallback use `isRegisteredDownloadContent` in
`src/utils/URLUtils.ts`. Explicit filename suffixes still take precedence; inline
media, unregistered MIME types and generic `application/octet-stream` responses
do not gain automatic capture. Existing blacklist, bypass and minimum-size checks
are unchanged.

Build and package from this directory:

```bash
npm ci
npm test
npm run pack
```

Artifacts: `dist/firefox.zip`, `dist/chrome.zip` and the unpacked browser-specific
directories. Firefox accepts the ZIP through **Install Add-on From File**; Chrome
development mode accepts `dist/chrome`. Builds are unsigned. Permanent Firefox/Zen
installation requires a browser that permits unsigned extensions and explicit
approval to change its signature policy; do not weaken that policy automatically.

The 10 regression tests cover extensionless attachment detection, RFC 5987 and ASCII
filenames, MIME aliases/parameters, inline responses, exclusions and suffix precedence.
Installed Zen state, live verification, security tradeoff and rollback are recorded in
[`PLASMA-LOCAL.md`](../../PLASMA-LOCAL.md#extensionless-browser-download-capture).

## Usage

In order to use this extension you need to [install](https://abdownloadmanager.com/#download) AB Download Manager.

### This extension does the following

- Adds a `Download With AB DM` in browser's context menu
- Automatically captures download links when the user wants to download the file from their browser
- Show a `Download Selected` popup when the user selects some section of the page that contains links

## Repositories And Source Code

There are multiple repositories related to the **AB Download Manager** project:

| Repository                                                                                                 | Description                                                                   |
|------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------|
| [Main Application](https://github.com/amir1376/ab-download-manager)                                        | Contains the  **Application** that runs on your  **device**                   |
| [Browser Integration](https://github.com/amir1376/ab-download-manager-browser-integration)  (You are here) | Contains the **Browser Extension** to be installed on your  **browser**       |
| [Website](https://github.com/amir1376/ab-download-manager-website)                                         | Contains the **AB Download Manager** [website](https://abdownloadmanager.com) |

I spent a lot of time to create this project.

If you like my work, Please consider giving it a ⭐.
Thanks ❤️
