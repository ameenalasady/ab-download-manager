#pragma once
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QTimer>
#include <algorithm>
#include <ctime>
#include <functional>
#include <memory>

// Opt-in, synthetic resize-to-frame latency probe. No profile/service access.
inline void profileResizing(QQuickView *view, const QString &path)
{
    struct Probe { QElapsedTimer frame, total; QList<double> samples; std::clock_t cpu; int step = 0; };
    auto p = std::make_shared<Probe>();
    auto next = std::make_shared<std::function<void()>>();
    *next = [view, path, p, next] {
        if (p->step == 180) {
            auto sorted = p->samples; std::sort(sorted.begin(), sorted.end());
            QJsonArray samples; for (auto s : p->samples) samples.append(s);
            QJsonObject data{{"samplesMs", samples}, {"p50Ms", sorted.at(90)}, {"p95Ms", sorted.at(171)},
                {"maxMs", sorted.last()}, {"wallMs", double(p->total.elapsed())},
                {"cpuMs", 1000.0 * (std::clock() - p->cpu) / CLOCKS_PER_SEC},
                {"graphicsApi", int(view->rendererInterface()->graphicsApi())}, {"dpr", view->devicePixelRatio()},
                {"resizeCount", p->step}, {"note", "synthetic client resize-to-frame probe, not a pointer/compositor latency benchmark"},
                {"backendEnv", qEnvironmentVariable("QT_QUICK_BACKEND")}, {"platform", QGuiApplication::platformName()}};
            QFile file(path); if (!file.open(QIODevice::WriteOnly)) { QCoreApplication::exit(5); return; }
            file.write(QJsonDocument(data).toJson()); file.close();
            *next = {}; QCoreApplication::quit(); return;
        }
        const int phase = p->step % 60;
        const int ramp = phase < 30 ? phase : 60 - phase;
        ++p->step; p->frame.start();
        view->resize(780 + ramp * 14, 610 + ramp * 7);
        view->requestUpdate();
    };
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(view, &QQuickWindow::frameSwapped, view, [view, p, next, connection] {
        if (!p->frame.isValid()) return;
        p->samples.append(p->frame.nsecsElapsed() / 1e6); p->frame.invalidate();
        if (p->samples.size() == 180) QObject::disconnect(*connection);
        QTimer::singleShot(0, view, *next);
    });
    QTimer::singleShot(1000, view, [p, next] { p->cpu = std::clock(); p->total.start(); (*next)(); });
}
