# AB Download Manager — Qt/KDE port

A personal fork of [AB Download Manager](https://github.com/amir1376/ab-download-manager)
for KDE Plasma on native Wayland. The original Compose desktop and Android apps are gone;
what remains is the original download engine, a Kotlin service that owns it, and a
Qt 6 Quick/Kirigami frontend.

| Path | What it is |
|---|---|
| [`desktop/service/`](desktop/service/README.md) | Kotlin service: runs the engine, keeps the original on-disk profile formats, serves the frontend over a private socket and the browser extension over the original HTTP API. |
| [`desktop/qt-ui/`](desktop/qt-ui/README.md) | Qt/KDE frontend (QML + C++ adapters). |
| [`desktop/qt-capability/`](desktop/qt-capability/README.md) | Native Wayland host executable for the frontend, plus the input-idle helper. |
| `downloader/core`, `downloader/monitor` | The original download engine and its progress monitor. |
| [`shared/categories`](shared/categories/README.md), [`shared/download-lifecycle`](shared/download-lifecycle/README.md), `shared/utils` | Engine-side pieces extracted from the original app. |
| `integration/server` | The original browser-integration HTTP endpoints. |
| [`integration/browser-extension/`](integration/browser-extension/README.md) | Browser extension with the extensionless-attachment capture fix. |
| [`scripts/qt-capability/`](scripts/qt-capability/README.md), [`scripts/qt-service/`](scripts/qt-service/) | Build, launcher, packaging and smoke-test tooling. |

## Build and test

Needs JDK 25, Qt 6.7+, KDE Frameworks 6 (Kirigami, qqc2-desktop-style), CMake and Ninja.
No Android SDK is needed.

```sh
export JAVA_HOME=/usr/lib/jvm/java-25-openjdk
./gradlew :desktop:service:test :desktop:service:installDist
./scripts/qt-capability/build.sh
```

Packaging and running a release: `scripts/qt-service/package.py` and
`scripts/qt-service/runtime.py`; see [`desktop/service/README.md`](desktop/service/README.md).
Full test commands are in [`desktop/qt-ui/TESTING.md`](desktop/qt-ui/TESTING.md).

## License

Apache-2.0, as upstream. See [LICENSE](LICENSE).
