#include "servicedownloads.h"
#include "previewdownloads.h"
#include <QDir>
#include <QGuiApplication>
#include <QClipboard>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QIcon>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QUrl>
#include <QtEndian>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr quint32 maxFrame = 2 * 1024 * 1024;
const QStringList sizeUnits{"BinaryBits", "BinaryBytes", "DecimalBits", "DecimalBytes"};
}

// Mirrors the original SizeConverter/SizeWithUnit "#.##" formatting: largest factor not
// above the value, binary (Ki/Mi…) or decimal (K/M…) factors, bytes (B) or bits (b).
QString ServiceDownloads::formatSize(qint64 bytes, const QString &unit) {
    if (bytes < 0) return QObject::tr("Unknown");
    const bool bits = unit.endsWith("Bits"), decimal = unit.startsWith("Decimal");
    const double base = decimal ? 1000.0 : 1024.0;
    double value = double(bytes) * (bits ? 8 : 1);
    static const char *binaryPrefixes[] = {"", "Ki", "Mi", "Gi", "Ti"};
    static const char *decimalPrefixes[] = {"", "K", "M", "G", "T"};
    int factor = 0;
    while (factor < 4 && value >= std::pow(base, factor + 1)) ++factor;
    value /= std::pow(base, factor);
    QString number = QString::number(value, 'f', 2);
    while (number.contains('.') && (number.endsWith('0') || number.endsWith('.'))) number.chop(1);
    return number + " " + QString::fromLatin1(decimal ? decimalPrefixes[factor] : binaryPrefixes[factor]) + (bits ? "b" : "B");
}

namespace {
QString sizeText(qint64 value, const QString &unit = QStringLiteral("BinaryBytes")) {
    return ServiceDownloads::formatSize(value, unit);
}
QString etaText(qint64 eta) {
    if (eta < 0) return QStringLiteral("—");
    if (eta < 60) return QObject::tr("%1 s").arg(eta);
    if (eta < 3600) return QObject::tr("%1 m %2 s").arg(eta / 60).arg(eta % 60);
    return QObject::tr("%1 h %2 m").arg(eta / 3600).arg((eta % 3600) / 60);
}
bool numericId(const QJsonValue &value) {
    bool ok = false; value.toString().toLongLong(&ok);
    return value.isString() && ok && !value.toString().startsWith('-');
}
bool matches(const QJsonObject &row, const QString &category) {
    const QString state = row["state"].toString();
    return category == "all" || category == state
        || (category == "active" && (state == "downloading" || state == "queued"))
        || (category == "unfinished" && state != "completed")
        || ServiceDownloads::fileCategory(row["name"].toString()) == category;
}
}

QString ServiceDownloads::fileCategory(const QString &name) {
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (QStringList{"zip", "gz", "xz", "zst", "7z", "rar", "tar"}.contains(suffix)) return "compressed";
    if (QStringList{"exe", "msi", "appimage", "deb", "rpm"}.contains(suffix)) return "programs";
    if (QStringList{"mp4", "webm", "mkv", "avi", "mov"}.contains(suffix)) return "videos";
    if (QStringList{"mp3", "flac", "ogg", "wav", "m4a"}.contains(suffix)) return "music";
    if (QStringList{"png", "jpg", "jpeg", "webp", "svg"}.contains(suffix)) return "pictures";
    return "documents";
}

// `name` as a plain regular file inside the row's download folder, or empty.
static QString plainFilePath(const QJsonObject &row, const QString &name) {
    const QString directory = row["details"].toObject()["folder"].toString();
    if (!QDir::isAbsolutePath(directory) || name.isEmpty() || name.contains('/') || name == "." || name == "..") return {};
    const QFileInfo dir(directory), file(QDir(directory).filePath(name));
    if (!dir.isDir() || file.isSymLink() || !file.isFile()) return {};
    return file.absoluteFilePath();
}

// The completed file's path when it is a plain regular file inside its download folder.
static QString completedFilePath(const QJsonObject &row) {
    return row["state"].toString() == "completed" ? plainFilePath(row, row["name"].toString()) : QString();
}

static QString themeIconFor(const QMimeType &mime) {
    if (QIcon::hasThemeIcon(mime.iconName())) return mime.iconName();
    // Generic names (video-x-generic…) exist in every icon theme; fall back to them without probing.
    return mime.genericIconName().isEmpty() ? QStringLiteral("text-x-generic") : mime.genericIconName();
}

// Icon from the file's MIME type: by name first, then by content (names such as "1Q4YkrC7" carry no
// extension). While downloading, the file being written is sniffed once it has bytes; its head may
// still be blank (sparse allocation), so a blank result is retried shortly.
QString ServiceDownloads::fileIcon(const QJsonObject &row) const {
    const QString name = row["name"].toString();
    static const QMimeDatabase db;
    const QMimeType byName = db.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
    if (!byName.isDefault()) return themeIconFor(byName);
    const bool completed = row["state"].toString() == "completed";
    const auto details = row["details"].toObject();
    const QString signature = name + '\n' + details["folder"].toString() + '\n' + details["partialName"].toString() + (completed ? "\ncompleted" : "");
    const QString id = row["id"].toString();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const auto cached = m_contentIcons.constFind(id);
    if (cached != m_contentIcons.constEnd() && cached->signature == signature && (cached->settled || now < cached->retryAt)) return cached->icon;
    QString path = completedFilePath(row);
    if (!completed && row["downloaded"].toString().toLongLong() > 0) path = plainFilePath(row, details["partialName"].toString());
    ContentIcon entry{signature, QStringLiteral("text-x-generic"), completed, now + 2000};
    if (!path.isEmpty()) {
        const QMimeType byContent = db.mimeTypeForFile(path, QMimeDatabase::MatchContent);
        const bool blank = byContent.isDefault() || byContent.name() == QLatin1String("application/x-zerosize");
        if (!blank || completed) entry.icon = themeIconFor(byContent);
        if (!blank) entry.settled = true;
    }
    m_contentIcons.insert(id, entry);
    return entry.icon;
}

// Same cleaning as the service's BrowserLinks.fileName: separators and control characters become
// "_", edge dots go, and an over-long name is cut while keeping a short extension.
static QString cleanFileName(QString value) {
    for (QChar &c : value) if (c == '/' || c == '\\' || c.category() == QChar::Other_Control) c = '_';
    value = value.trimmed();
    while (value.startsWith('.')) value.remove(0, 1);
    while (value.endsWith('.')) value.chop(1);
    if (value.size() > 180) {
        const QString extension = value.contains('.') ? value.section('.', -1) : QString();
        value = extension.size() >= 1 && extension.size() <= 16 ? value.left(179 - extension.size()) + '.' + extension : value.left(180);
    }
    return value;
}
// A name without a known extension gets the one for the server's media type ("video/mp4" -> ".mp4").
static QString withTypeExtension(const QString &name, const QString &contentType) {
    static const QMimeDatabase db;
    if (name.isEmpty() || contentType.isEmpty()
        || QStringList{"application/octet-stream", "binary/octet-stream", "text/html"}.contains(contentType)
        || !db.mimeTypeForFile(name, QMimeDatabase::MatchExtension).isDefault()) return name;
    const QMimeType type = db.mimeTypeForName(contentType);
    const QString suffix = type.isValid() && !type.isDefault() ? type.preferredSuffix() : QString();
    if (suffix.isEmpty()) return name;
    return name.left(180 - 1 - suffix.size()) + '.' + suffix;
}
ServiceDownloads::ServiceDownloads(const QString &descriptor, QObject *parent)
    : QAbstractListModel(parent), m_descriptor(descriptor), m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    m_socket.setReadBufferSize(maxFrame + 4);
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (m_socket.state() == QLocalSocket::UnconnectedState) connectService();
        else if (m_state == "ready" && !m_waiting) request({{"type", "snapshot"}});
    });
    m_deadline.setSingleShot(true); m_deadline.setInterval(5000);
    connect(&m_deadline, &QTimer::timeout, this, [this] { fail(tr("Service timed out. Reconnecting; commands are not replayed.")); m_socket.abort(); disconnected(); });
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        // "frontend": browser requests then wait for this window instead of launching another.
        request({{"type", "hello"}, {"version", 1}, {"token", m_token}, {"clientId", m_clientId}, {"role", "frontend"}});
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &ServiceDownloads::readFrames);
    connect(&m_socket, &QLocalSocket::disconnected, this, &ServiceDownloads::disconnected);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this] { disconnected(); });
    m_poll.start();
    QTimer::singleShot(0, this, &ServiceDownloads::connectService);
}

void ServiceDownloads::connectService() {
    struct stat st{};
    const QByteArray path = QFile::encodeName(m_descriptor);
    if (::lstat(path.constData(), &st) || !S_ISREG(st.st_mode) || st.st_uid != ::getuid()
        || (st.st_mode & 0077) || st.st_size > 4096) {
        fail(tr("Service descriptor unavailable or not private. Start the isolated service.")); return;
    }
    QFile file(m_descriptor);
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(file.read(4097));
    const auto descriptor = doc.object();
    if (descriptor["version"].toInt() != 1 || descriptor["token"].toString().size() != 43) {
        fail(tr("Unsupported service descriptor.")); return;
    }
    const QString socket = descriptor["socket"].toString();
    const QByteArray socketPath = QFile::encodeName(socket);
    struct stat socketStat{}, dirStat{};
    const QByteArray directory = QFile::encodeName(QFileInfo(socket).absolutePath());
    if (::lstat(socketPath.constData(), &socketStat) || !S_ISSOCK(socketStat.st_mode)
        || socketStat.st_uid != ::getuid() || (socketStat.st_mode & 0077)
        || ::lstat(directory.constData(), &dirStat) || !S_ISDIR(dirStat.st_mode)
        || dirStat.st_uid != ::getuid() || (dirStat.st_mode & 0077)) {
        fail(tr("Service endpoint is not a private local socket.")); return;
    }
    m_token = descriptor["token"].toString(); m_epoch = descriptor["epoch"].toString();
    m_state = "connecting"; m_input.clear(); m_expected = 0; m_waiting = false;
    emit viewChanged(); emit selectionChanged();
    m_deadline.start(); m_socket.connectToServer(socket);
}

void ServiceDownloads::request(const QJsonObject &request) {
    auto outgoing = request;
    if (outgoing["type"] == "snapshot") {
        outgoing["active"] = m_frontendActive;
        if (m_capabilities.contains("download.parts")) {
            QJsonArray ids; auto keys = m_partWatches.keys(); std::sort(keys.begin(), keys.end());
            for (const auto &id : keys) ids.append(id);
            outgoing["partsFor"] = ids;
        }
    }
    const QByteArray payload = QJsonDocument(outgoing).toJson(QJsonDocument::Compact);
    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(payload.size(), frame.data()); frame.append(payload);
    m_waiting = true; m_deadline.start(); m_socket.write(frame);
}

void ServiceDownloads::readFrames() {
    m_input += m_socket.readAll();
    auto invalid = [this] { fail(tr("Invalid service response. Connection closed.")); m_socket.abort(); disconnected(); };
    if (m_input.size() > maxFrame + 4) { invalid(); return; }
    if (!m_expected && m_input.size() >= 4) {
        m_expected = qFromBigEndian<quint32>(m_input.constData()); m_input.remove(0, 4);
        if (!m_expected || m_expected > maxFrame) { invalid(); return; }
    }
    if (!m_expected || m_input.size() < m_expected) return;
    if (m_input.size() != m_expected || !m_waiting) { invalid(); return; }
    QJsonParseError error;
    const auto reply = QJsonDocument::fromJson(m_input, &error).object();
    m_input.clear(); m_expected = 0; m_waiting = false; m_deadline.stop();
    if (error.error != QJsonParseError::NoError || reply["epoch"].toString() != m_epoch) { invalid(); return; }
    const QString type = reply["type"].toString();
    if (m_state == "connecting" && type == "hello" && reply["version"].toInt() == 1) {
        const auto caps = reply["capabilities"].toArray();
        for (const auto &cap : {"snapshot", "add", "pause", "resume"})
            if (!caps.contains(QString::fromLatin1(cap))) { invalid(); return; }
        m_capabilities.clear();
        for (const auto &cap : caps) if (cap.isString()) m_capabilities.append(cap.toString());
        m_state = "ready"; if (!m_uncertain) clearError(); emit viewChanged(); emit selectionChanged();
        request({{"type", "snapshot"}});
    } else if (type == "serviceError") {
        // The service replaced a reply with a bounded-frame error, so nothing about an in-flight
        // command may be assumed. Resolve it here: queued batch steps are dropped (never sent
        // later) and a transmitted command is reported as unknown, never as success.
        const QString target = m_commandTarget;
        QStringList notices{reply["error"].toString(tr("Service error"))};
        if (!m_pending.isEmpty()) {
            if (m_deferred.isEmpty()) { m_uncertain = true; notices << tr("The command outcome is unknown; inspect the refreshed list before retrying."); }
            else notices << tr("The pending command was not sent because the service rejected the connection state.");
        }
        if (!m_batch.isEmpty())
            notices << tr("%n further action(s) were not sent and will not be sent automatically.", nullptr, int(m_batch.size()));
        m_batch.clear(); m_deferred = {}; m_pending.clear(); m_commandOperation.clear(); m_commandTarget.clear();
        fail(notices.join(' '), target);
        m_state = "error";
        emit viewChanged(); emit selectionChanged();
    } else if (m_state == "ready" && type == "snapshot") {
        const auto snapshot = reply["snapshot"].toObject();
        if (snapshot["state"].toString() != "ready" || !snapshot["rows"].isArray()) { invalid(); return; }
        QVector<QJsonObject> rows; QSet<QString> ids;
        for (const auto &value : snapshot["rows"].toArray()) {
            const auto row = value.toObject(); const auto id = row["id"].toString();
            bool idOk = false, sizeOk = false, doneOk = false;
            id.toLongLong(&idOk); const auto size = row["size"].toString().toLongLong(&sizeOk);
            const auto done = row["downloaded"].toString().toLongLong(&doneOk);
            if (!idOk || id.isEmpty() || ids.contains(id) || !sizeOk || size < -1 || !doneOk || done < 0
                || row["name"].toString().isEmpty()
                || !QStringList{"queued", "downloading", "paused", "completed", "failed"}.contains(row["state"].toString())) { invalid(); return; }
            if (m_capabilities.contains("monitor.stats")) {
                bool speedOk = false, etaOk = false;
                const auto speed = row["speed"].toString().toLongLong(&speedOk);
                const auto eta = row["remainingTime"].toString().toLongLong(&etaOk);
                if (!speedOk || speed < -1 || !etaOk || eta < -1) { invalid(); return; }
            }
            if (m_capabilities.contains("download.details")) {
                const auto details = row["details"].toObject();
                if (!details["url"].isString() || details["url"].toString().size() > 2048
                    || !details["folder"].isString() || details["folder"].toString().size() > 32768
                    || (!details["error"].isNull() && !details["error"].isObject())) { invalid(); return; }
                if (details["error"].isObject()) {
                    const auto reason = details["error"].toObject();
                    for (const auto &key : {"title", "description", "suggestion", "throwableName"}) {
                        const int bound = QString::fromLatin1(key) == "title" || QString::fromLatin1(key) == "throwableName" ? 512 : 8192;
                        if (!reason[key].isString() || reason[key].toString().size() > bound) { invalid(); return; }
                    }
                    if ((!reason["throwableMessage"].isNull() && !reason["throwableMessage"].isString())
                        || reason["throwableMessage"].toString().size() > 8192) { invalid(); return; }
                }
            }
            if (m_capabilities.contains("download.checksum")) {
                const auto saved = row["details"].toObject()["fileChecksum"];
                if ((!saved.isNull() && !saved.isString()) || saved.toString().size() > 256) { invalid(); return; }
            }
            if (m_capabilities.contains("download.links")) {
                const auto page = row["details"].toObject()["downloadPage"];
                if (!page.isString() || page.toString().size() > 2048) { invalid(); return; }
                const QUrl url(page.toString(), QUrl::StrictMode);
                if (!page.toString().isEmpty() && (!url.isValid() || !QStringList{"http", "https"}.contains(url.scheme())
                    || url.host().isEmpty() || !url.userInfo().isEmpty())) { invalid(); return; }
            }
            if (m_capabilities.contains("download.http-options")) {
                const auto details = row["details"].toObject(); bool speedOk = false;
                const auto speed = details["speedLimit"].toString().toLongLong(&speedOk);
                const auto connections = details["connections"];
                if (!speedOk || speed < 0 || !details["hasCredentials"].isBool()
                    || (!connections.isNull() && (!connections.isDouble() || connections.toInt() < 1 || connections.toInt() > 256))) { invalid(); return; }
            }
            ids.insert(id); rows.append(row);
        }
        // Optional extensions are accepted only when advertised; malformed extension data
        // must not replace the last coherent settings/queue snapshot.
        QVariantList queues;
        if (m_capabilities.contains("queue.create")) {
            if (!snapshot["queues"].isArray()) { invalid(); return; }
            QSet<QString> queueIds;
            for (const auto &value : snapshot["queues"].toArray()) {
                const auto q = value.toObject(); bool ok = false;
                const QString id = q["id"].toString(); id.toLongLong(&ok);
                if (!ok || queueIds.contains(id) || q["name"].toString().isEmpty()
                    || !q["active"].isBool() || !q["stopOnEmpty"].isBool()
                    || !q["items"].isArray() || q["maxConcurrent"].toInt() < 1 || q["maxConcurrent"].toInt() > 1024) { invalid(); return; }
                if (m_capabilities.contains("queue.powerAction") && !(q["powerAction"].isNull()
                    || QStringList{"Shutdown", "Sleep", "Hibernate"}.contains(q["powerAction"].toString()))) { invalid(); return; }
                QSet<QString> members;
                for (const auto &item : q["items"].toArray()) {
                    const auto member = item.toString(); member.toLongLong(&ok);
                    if (!ok || members.contains(member)) { invalid(); return; }
                    members.insert(member);
                }
                if (m_capabilities.contains("queue.schedule")) {
                    const auto schedule = q["schedule"].toObject();
                    const auto days = schedule["daysOfWeek"].toArray();
                    const QStringList allowedDays{"MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY"};
                    QSet<QString> uniqueDays;
                    for (const auto &day : days) {
                        if (!allowedDays.contains(day.toString()) || uniqueDays.contains(day.toString())) { invalid(); return; }
                        uniqueDays.insert(day.toString());
                    }
                    auto validTime = [](const QString &time) { return QTime::fromString(time, "HH:mm").isValid() || QTime::fromString(time, Qt::ISODate).isValid(); };
                    if (days.isEmpty() || !schedule["enabledStartTime"].isBool() || !schedule["enabledEndTime"].isBool()
                        || !validTime(schedule["startTime"].toString()) || !validTime(schedule["endTime"].toString())) { invalid(); return; }
                }
                queueIds.insert(id); queues.append(q.toVariantMap());
            }
        }
        QVariantList categories;
        QHash<QString, QSet<QString>> categoryMembers;
        if (m_capabilities.contains("category.create")) {
            if (!snapshot["categories"].isArray() || snapshot["categories"].toArray().size() > 128) { invalid(); return; }
            QSet<QString> categoryIds;
            for (const auto &value : snapshot["categories"].toArray()) {
                const auto c = value.toObject(); const auto id = c["id"].toString(); bool ok = false;
                const auto numericId = id.toLongLong(&ok);
                auto stringArray = [](const QJsonValue &v, int bound, int maxLength) {
                    if (!v.isArray() || v.toArray().size() > bound) return false;
                    for (const auto &s : v.toArray()) if (!s.isString() || s.toString().isEmpty() || s.toString().size() > maxLength) return false;
                    return true;
                };
                if (!ok || numericId < 0 || categoryIds.contains(id) || c["name"].toString().isEmpty()
                    || c["name"].toString().size() > 120 || !c["icon"].isString() || !c["path"].isString() || !c["usePath"].isBool()
                    || !c["items"].isArray() || !stringArray(c["acceptedFileTypes"], 128, 40)
                    || !stringArray(c["acceptedUrlPatterns"], 32, 512)) { invalid(); return; }
                QSet<QString> members;
                for (const auto &member : c["items"].toArray()) {
                    const auto memberId = member.toString(); memberId.toLongLong(&ok);
                    if (!ok || members.contains(memberId)) { invalid(); return; }
                    members.insert(memberId);
                }
                categoryIds.insert(id); categoryMembers.insert("category:" + id, members); categories.append(c.toVariantMap());
            }
        }
        const auto settings = snapshot["settings"].toObject().toVariantMap();
        if (m_capabilities.contains("settings.speed")) {
            const auto s = snapshot["settings"].toObject(); bool ok = false;
            const auto speed = s["speedLimit"].toString().toLongLong(&ok);
            // The activity-dependent original mode is only meaningful with the policy extension.
            const QStringList modes = m_capabilities.contains("settings.policy") ? QStringList{"Enabled", "Disabled", "EnabledWhenBessy"} : QStringList{"Enabled", "Disabled"};
            if (!ok || speed <= 0 || !s["useAverageSpeed"].isBool() || !modes.contains(s["useSpeedLimit"].toString())) { invalid(); return; }
        }
        if (m_capabilities.contains("settings.display")) {
            const auto s = snapshot["settings"].toObject();
            if (!sizeUnits.contains(s["sizeUnit"].toString()) || !sizeUnits.contains(s["speedUnit"].toString())
                || !s["useCategoryByDefault"].isBool()) { invalid(); return; }
        }
        if (m_capabilities.contains("settings.sort") && (!QStringList{"name", "size", "state", "speed", "remainingTime", "dateAdded"}.contains(settings.value("qtSortColumn").toString())
            || !snapshot["settings"].toObject()["qtSortDescending"].isBool())) { invalid(); return; }
        if (m_capabilities.contains("settings.duplicates")
            && !QStringList{"ask", "numbered", "existing"}.contains(snapshot["settings"].toObject()["qtDuplicateAction"].toString())) { invalid(); return; }
        QVariantMap settingsPolicy;
        if (m_capabilities.contains("settings.policy")) {
            const auto policy = snapshot["settingsPolicy"].toObject();
            const auto retained = policy["retained"].toArray();
            if (!policy["retained"].isArray() || retained.size() > 64 || !policy["activityAdapter"].isBool() || !policy["speedLimitActive"].isBool()) { invalid(); return; }
            static const QRegularExpression key(QStringLiteral("^[A-Za-z]{1,64}$"));
            for (const auto &value : retained) {
                const auto entry = value.toObject(); const auto v = entry["value"];
                if (!key.match(entry["key"].toString()).hasMatch() || !QStringList{"platform", "runtime-override"}.contains(entry["policy"].toString())
                    || !entry["stored"].isBool() || !(v.isNull() || v.isBool() || (v.isDouble() && std::abs(v.toDouble()) <= 2147483647.0))) { invalid(); return; }
            }
            settingsPolicy = policy.toVariantMap();
        }
        if (m_capabilities.contains("settings.update")) {
            const auto s = snapshot["settings"].toObject();
            bool minPartOk = false;
            const auto minPart = s["minPartSize"].toString().toLongLong(&minPartOk);
            if (!s["threadCount"].isDouble() || s["threadCount"].toInt() < 1 || s["threadCount"].toInt() > 256
                || !s["maxConcurrentDownloads"].isDouble() || s["maxConcurrentDownloads"].toInt(-1) < 0 || s["maxConcurrentDownloads"].toInt() > 1024
                || !s["maxDownloadRetryCount"].isDouble() || s["maxDownloadRetryCount"].toInt(-1) < 0 || s["maxDownloadRetryCount"].toInt() > 1024
                || !minPartOk || minPart < 1024 || minPart > 2147483648LL
                || !s["dynamicPartCreation"].isBool() || !s["useSparseFileAllocation"].isBool() || !s["useServerLastModifiedTime"].isBool()) { invalid(); return; }
        }
        QVariantMap runtime;
        if (m_capabilities.contains("runtime.policy")) {
            const auto r = snapshot["runtime"].toObject();
            const auto roots = r["destinationRoots"].toArray();
            if (!QStringList{"isolated", "explicit-runtime"}.contains(r["mode"].toString())
                || !r["destination"].isString() || r["destination"].toString().isEmpty()
                || r["destination"].toString().size() > 32768 || !r["destinationRoots"].isArray()
                || roots.isEmpty() || roots.size() > 16
                || !r["redirects"].isBool() || !QStringList{"direct", "explicit"}.contains(r["proxy"].toString())) { invalid(); return; }
            for (const auto &path : roots)
                if (!path.isString() || path.toString().isEmpty() || path.toString().size() > 32768) { invalid(); return; }
            // The default folder (runtime.destination) is always one of, or inside, the approved
            // roots; a destination outside them would silently widen where new files may land.
            const QString destination = QDir::cleanPath(r["destination"].toString());
            bool insideRoots = false;
            for (const auto &value : roots) {
                const QString root = QDir::cleanPath(value.toString());
                if (root != QLatin1String("/") && (destination == root || destination.startsWith(root + '/'))) insideRoots = true;
            }
            if (!QDir::isAbsolutePath(destination) || !insideRoots) { invalid(); return; }
            runtime = r.toVariantMap();
        }
        QVariantList browserRequests;
        if (m_capabilities.contains("browser.intake")) {
            const auto requests = snapshot["browserRequests"];
            if (!requests.isArray() || requests.toArray().size() > 100) { invalid(); return; }
            int total = 0; QSet<QString> requestIds;
            for (const auto &value : requests.toArray()) {
                const auto r = value.toObject(); const auto id = r["id"].toString();
                const auto items = r["items"].toArray();
                if ((!r["outcomeUnknown"].isUndefined() && !r["outcomeUnknown"].isBool())
                    || id.isEmpty() || id.size() > 64 || requestIds.contains(id) || !r["items"].isArray() || items.isEmpty()
                    || (total += items.size()) > 100) { invalid(); return; }
                QVariantList itemMaps;
                for (const auto &entry : items) {
                    const auto item = entry.toObject();
                    if (!item["url"].isString() || item["url"].toString().isEmpty() || item["url"].toString().size() > 2048
                        || !item["name"].isString() || item["name"].toString().isEmpty() || item["name"].toString().size() > 180
                        || !item["hasHeaders"].isBool()
                        || !(item["downloadPage"].isNull() || (item["downloadPage"].isString() && item["downloadPage"].toString().size() <= 2048))) { invalid(); return; }
                    QVariantMap map = item.toVariantMap();
                    map.remove("probe");
                    if (item.contains("probe")) {
                        // browser.probeBatch outcome; only display values reach QML.
                        const auto p = item["probe"].toObject(); const auto state = p["state"].toString();
                        bool sizeOk = false; const qint64 size = p["size"].toString().toLongLong(&sizeOk);
                        if (!item["probe"].isObject() || !QStringList{"pending", "done", "failed"}.contains(state)
                            || (state == "done" && (!sizeOk || size < -1 || !p["statusCode"].isDouble() || !p["requiresAuth"].isBool()))) { invalid(); return; }
                        const auto shown = state == "done" ? probeSize(p["requiresAuth"].toBool(), p["statusCode"].toInt(), size)
                            : qMakePair(QStringLiteral("-1"), state == "pending" ? tr("Checking…") : tr("Unavailable"));
                        map["probeState"] = state; map["size"] = shown.first; map["sizeLabel"] = shown.second;
                    }
                    itemMaps.append(map);
                }
                auto request = r.toVariantMap(); request["items"] = itemMaps;
                requestIds.insert(id); browserRequests.append(request);
            }
        }
        QVariantMap browserIntegration;
        if (m_capabilities.contains("browser.integration")) {
            const auto b = snapshot["browserIntegration"].toObject();
            if (!QStringList{"disabled", "starting", "listening", "failed", "unavailable"}.contains(b["state"].toString())
                || !b["enabled"].isBool() || !b["auth"].isBool() || b["port"].toInt() < 1 || b["port"].toInt() > 65535
                || !(b["error"].isNull() || (b["error"].isString() && b["error"].toString().size() <= 240))) { invalid(); return; }
            browserIntegration = b.toVariantMap();
        }
        QVariantMap integration;
        if (m_capabilities.contains("settings.integration")) {
            const auto i = snapshot["integration"].toObject();
            for (const auto &key : {"autoStartOnBoot", "useSystemTray", "notificationSound", "notifications"})
                if (!i[key].isBool()) { invalid(); return; }
            if (m_capabilities.contains("download.windows"))
                for (const auto &key : {"showDownloadProgressDialog", "showDownloadCompletionDialog"})
                    if (!i[key].isBool()) { invalid(); return; }
            if (m_capabilities.contains("settings.notificationSounds"))
                for (const auto &key : {"successNotificationSound", "errorNotificationSound", "generalNotificationSound"})
                    if (!i[key].isString() || i[key].toString().size() > 4096) { invalid(); return; }
            integration = i.toVariantMap();
        }
        QVariantMap powerAction;
        if (m_capabilities.contains("queue.powerAction") && !snapshot["powerAction"].isNull()) {
            const auto p = snapshot["powerAction"].toObject();
            const auto serial = p["serial"].toString();
            if (!snapshot["powerAction"].isObject() || !numericId(p["serial"]) || serial.toLongLong() <= 0
                || QString::number(serial.toLongLong()) != serial || !numericId(p["queueId"])
                || !QStringList{"Shutdown", "Sleep", "Hibernate"}.contains(p["type"].toString())
                || !QStringList{"queueFinished", "queueEndTime"}.contains(p["reason"].toString())
                || !p["queueName"].isString() || p["queueName"].toString().isEmpty() || p["queueName"].toString().size() > 120) { invalid(); return; }
            powerAction = p.toVariantMap();
        }
        // Present in every service snapshot; optional so older/fake services stay compatible.
        QSet<QString> manualPending;
        if (snapshot.contains("manualPending")) {
            const auto pendingIds = snapshot["manualPending"];
            if (!pendingIds.isArray() || pendingIds.toArray().size() > rows.size()) { invalid(); return; }
            for (const auto &id : pendingIds.toArray()) {
                if (!numericId(id) || !ids.contains(id.toString()) || manualPending.contains(id.toString())) { invalid(); return; }
                manualPending.insert(id.toString());
            }
        }
        QVariantMap parts;
        if (m_capabilities.contains("download.parts")) {
            if (!snapshot["downloadParts"].isObject()) { invalid(); return; }
            const auto projection = snapshot["downloadParts"].toObject();
            if (projection.size() > 16) { invalid(); return; }
            for (auto it = projection.begin(); it != projection.end(); ++it) {
                const auto p = it.value().toObject(); const auto items = p["items"].toArray();
                const int total = p["totalCount"].toInt(-1);
                if (!numericId(it.key()) || !p["available"].isBool() || !p["truncated"].isBool()
                    || !p["items"].isArray() || items.size() > 256 || total < items.size()
                    || p["truncated"].toBool() != (total > 256)
                    || items.size() != qMin(total, 256)
                    || (!p["resumeSupport"].isNull() && !p["resumeSupport"].isBool())) { invalid(); return; }
                QVariantList ranges; QSet<QString> partIds;
                for (const auto &value : items) {
                    const auto r = value.toObject(); bool offOk = false, doneOk = false, lengthOk = false;
                    const auto offset = r["offset"].toString().toLongLong(&offOk);
                    const auto done = r["downloaded"].toString().toLongLong(&doneOk);
                    const auto length = r["length"].toString().toLongLong(&lengthOk);
                    if (!offOk || offset < 0 || !doneOk || done < 0 || r["id"] != r["offset"] || partIds.contains(r["id"].toString())
                        || (!r["length"].isNull() && (!lengthOk || length < 0 || done > length))
                        || !QStringList{"idle", "connecting", "downloading", "completed", "disconnected"}.contains(r["state"].toString())) { invalid(); return; }
                    partIds.insert(r["id"].toString());
                    // Whitelist only. Future protocol extensions cannot expose secrets to QML.
                    ranges.append(QVariantMap{{"id", r["id"].toString()}, {"offset", r["offset"].toString()},
                        {"length", r["length"].toVariant()}, {"downloaded", r["downloaded"].toString()}, {"state", r["state"].toString()},
                        {"progress", lengthOk && length > 0 ? double(done) / length : -1.0},
                        {"downloadedLabel", sizeText(done, settings.value("sizeUnit", "BinaryBytes").toString())},
                        {"sizeLabel", lengthOk ? sizeText(length, settings.value("sizeUnit", "BinaryBytes").toString()) : tr("Unknown")}});
                }
                parts.insert(it.key(), QVariantMap{{"available", p["available"].toBool()}, {"totalCount", total},
                    {"truncated", p["truncated"].toBool()}, {"resumeSupport", p["resumeSupport"].toVariant()}, {"items", ranges}});
            }
        }
        // Validate the whole hint ring before applying any part of this snapshot.
        qint64 windowSerial = -1;
        QVector<QPair<QString, QString>> windowEvents;
        if (m_capabilities.contains("download.windows")) {
            const auto w = snapshot["downloadWindows"].toObject(); bool ok = false;
            windowSerial = w["serial"].toString().toLongLong(&ok);
            const auto events = w["events"].toArray();
            if (!ok || windowSerial < 0 || !w["events"].isArray() || events.size() > 64
                || (m_windowEpoch == m_epoch && windowSerial < m_windowSerial)) { invalid(); return; }
            qint64 previous = 0;
            for (const auto &value : events) {
                const auto e = value.toObject(); bool serialOk = false;
                const auto serial = e["serial"].toString().toLongLong(&serialOk);
                if (!serialOk || serial <= previous || serial > windowSerial || !numericId(e["id"])
                    || !QStringList{"started", "completed", "failed"}.contains(e["kind"].toString())) { invalid(); return; }
                previous = serial;
                if (!m_windowBaseline && m_windowEpoch == m_epoch && serial > m_windowSerial) windowEvents.append({e["id"].toString(), e["kind"].toString()});
            }
            if (!events.isEmpty() && previous != windowSerial) { invalid(); return; }
        }
        QVariantMap checksum;
        if (m_capabilities.contains("download.checksum")) {
            const auto value = snapshot["checksum"];
            if (!value.isNull()) {
                const auto c = value.toObject();
                const QHash<QString, int> lengths{{"MD5", 32}, {"SHA-1", 40}, {"SHA-256", 64}, {"SHA-512", 128}};
                const auto digest = c["digest"].toString(), state = c["state"].toString();
                if (!value.isObject() || !numericId(c["id"]) || !lengths.contains(c["algorithm"].toString())
                    || !QStringList{"running", "done", "cancelled", "error"}.contains(state)
                    || !c["percent"].isDouble() || c["percent"].toDouble() != c["percent"].toInt(-1)
                    || c["percent"].toInt() < 0 || c["percent"].toInt() > 100 || !c["digest"].isString()
                    || (state == "done" ? (c["percent"].toInt() != 100 || digest.size() != lengths.value(c["algorithm"].toString())
                        || std::any_of(digest.cbegin(), digest.cend(), [](QChar ch) { return !QStringLiteral("0123456789abcdef").contains(ch); })) : !digest.isEmpty())) { invalid(); return; }
                checksum = c.toVariantMap();
            }
        }
        QJsonObject activation;
        if (m_capabilities.contains("frontend.activate")) {
            const auto value = snapshot["frontend"].toObject()["activation"];
            if (!value.isNull()) {
                activation = value.toObject(); bool ok = false;
                activation["serial"].toString().toLongLong(&ok);
                const auto token = activation["token"], id = activation["downloadId"];
                if (!ok || !QStringList{"show", "review"}.contains(activation["action"].toString())
                    || !(token.isNull() || (token.isString() && token.toString().size() <= 256))
                    || !(id.isNull() || id.isString())) { invalid(); return; }
            }
        }
        const bool browserUpdated = browserRequests != m_browserRequests || browserIntegration != m_browserIntegration;
        const bool runtimeChanged = runtime != m_runtime;
        const bool categoriesChanged = categories != m_categories;
        const bool unitsChanged = settings.value("sizeUnit") != m_settings.value("sizeUnit") || settings.value("speedUnit") != m_settings.value("speedUnit");
        const bool sortChanged = settings.value("qtSortColumn") != m_settings.value("qtSortColumn") || settings.value("qtSortDescending") != m_settings.value("qtSortDescending");
        const bool extensionsChanged = queues != m_queues || settings != m_settings || categoriesChanged || runtimeChanged || settingsPolicy != m_settingsPolicy || integration != m_integration
            || manualPending != m_manualPending || parts != m_parts || powerAction != m_powerAction || checksum != m_checksum;
        if (extensionsChanged) {
            m_queues = queues; m_settings = settings; m_categories = categories; m_categoryMembers = categoryMembers; m_runtime = runtime;
            m_settingsPolicy = settingsPolicy; m_integration = integration; m_manualPending = manualPending; m_parts = parts;
            m_powerAction = powerAction; m_checksum = checksum;
            if (m_category.startsWith("category:") && !m_categoryMembers.contains(m_category)) m_category = "all";
        }
        if (rows != m_rows || categoriesChanged || unitsChanged || sortChanged || !m_selectOnSnapshot.isEmpty()) {
            m_rows = rows;
            if (!m_selectOnSnapshot.isEmpty()) {
                m_selected = m_anchor = m_selectOnSnapshot; m_selection = {m_selectOnSnapshot}; m_selectOnSnapshot.clear();
            }
            rebuild();
            emit selectionChanged();
        } else if (extensionsChanged) { emit viewChanged(); emit selectionChanged(); }
        if (!m_acceptedRequest.isEmpty()) {
            const auto requestId = m_acceptedRequest; const auto acceptedIds = m_acceptedIds; const bool start = m_acceptedStart;
            m_acceptedRequest.clear(); m_acceptedIds.clear();
            emit browserRequestAccepted(requestId, acceptedIds, start);
        }
        if (!m_dismissedRequest.isEmpty()) {
            const auto requestId = m_dismissedRequest; m_dismissedRequest.clear(); emit browserRequestDismissed(requestId);
        }
        // Last: a browser review opened from this signal must see this snapshot's runtime/categories.
        if (browserUpdated) {
            m_browserRequests = browserRequests; m_browserIntegration = browserIntegration; emit browserChanged();
            // Report each review row's Check Sizes progress once per change.
            QHash<QString, QString> seen;
            for (const auto &value : std::as_const(m_browserRequests)) {
                const auto request = value.toMap(); const auto items = request["items"].toList();
                for (int index = 0; index < items.size(); ++index) {
                    const auto item = items[index].toMap();
                    if (!item.contains("probeState")) continue;
                    const QString key = request["id"].toString() + '\n' + QString::number(index) + '\n' + item["url"].toString();
                    const QString outcome = item["probeState"].toString() + '\n' + item["sizeLabel"].toString();
                    seen.insert(key, outcome);
                    if (m_probeSeen.value(key) != outcome)
                        emit browserMetadataReady(request["id"].toString(), index, item["size"].toString(), item["sizeLabel"].toString(), item["probeState"].toString());
                }
            }
            m_probeSeen = seen;
        }
        if (!activation.isEmpty() && m_activationKey != m_epoch + ":" + activation["serial"].toString()) {
            m_activationKey = m_epoch + ":" + activation["serial"].toString();
            emit activationRequested(activation["token"].toString(), activation["action"].toString(), activation["downloadId"].toString());
        }
        if (windowSerial >= 0) {
            // First snapshot of an epoch is a baseline: never open windows for earlier events,
            // whether this frontend just started or the service restarted. Rows are current.
            if (m_windowEpoch != m_epoch) m_windowEpoch = m_epoch;
            m_windowSerial = windowSerial; m_windowBaseline = false;
            for (const auto &event : std::as_const(windowEvents)) emit downloadWindowRequested(event.first, event.second);
        }
        if (!m_deferred.isEmpty()) { const auto next = m_deferred; m_deferred = {}; request(next); }
    } else if (m_state == "ready" && type == "reply" && !m_pending.isEmpty()
               && reply["commandId"].toString() == m_pending) {
        m_pending.clear();
        const auto result = reply["result"].toObject();
        if (m_commandOperation == "download.probe" && !result["ok"].toBool()) {
            // An unreachable link is not an error for the dialog: it keeps the name from the link.
            reportLinkProbe(QString(), QString(), false);
        } else if (m_commandOperation == "browser.probe" && !result["ok"].toBool()) {
            // One unreachable link is that row's answer, not a reason to skip the rest.
            emit browserMetadataReady(m_probeRequest, m_probeIndex, QStringLiteral("-1"), tr("Unavailable"), QStringLiteral("failed"));
        } else if (!result["ok"].toBool()) {
            fail(result["error"].toString(tr("Command failed")), m_commandTarget);
            if (!m_batch.isEmpty()) dropBatch(tr("The remaining %n action(s) were not sent.", nullptr, int(m_batch.size())));
        } else if (m_batch.isEmpty()) clearError();
        m_commandTarget.clear();
        if ((m_commandOperation == "add" || m_commandOperation == "browser.accept") && !result["id"].toString().isEmpty()) {
            // Also select a committed Add whose separate category write failed. Keep its
            // explicit partial-outcome warning visible; never replay or manufacture success.
            m_category = "all"; m_search.clear(); m_selectOnSnapshot = result["id"].toString();
            emit viewChanged();
        }
        if (m_commandOperation == "browser.accept" && result["ok"].toBool()) {
            const auto ids = result["ids"].toArray(); QStringList accepted;
            if (ids.isEmpty() || ids.size() > 100) { invalid(); return; }
            for (const auto &id : ids) {
                if (!numericId(id) || accepted.contains(id.toString())) { invalid(); return; }
                accepted.append(id.toString());
            }
            m_acceptedRequest = m_browserCommandId; m_acceptedIds = accepted; m_acceptedStart = m_browserCommandStart;
        }
        if (m_commandOperation == "browser.probe" && result["ok"].toBool()) {
            bool ok = false; const auto size = result["size"].toString().toLongLong(&ok);
            const int index = result["index"].toInt(-1); const QString requestId = result["requestId"].toString();
            if (!ok || size < -1 || index < 0 || index >= 100 || requestId.isEmpty() || requestId.size() > 64) { invalid(); return; }
            const auto shown = probeSize(result["requiresAuth"].toBool(), result["statusCode"].toInt(), size);
            emit browserMetadataReady(requestId, index, shown.first, shown.second, QStringLiteral("done"));
        }
        if (m_commandOperation == "download.probe" && result["ok"].toBool()) {
            bool ok = false; const auto size = result["size"].toString().toLongLong(&ok);
            if (!ok || size < -1) { invalid(); return; }
            const int status = result["statusCode"].toInt(); const bool requiresAuth = result["requiresAuth"].toBool();
            const bool fromServer = status >= 200 && status < 300 && !requiresAuth;
            const QString name = cleanFileName(result["suggestedName"].toString());
            // An error page's own name or type says nothing about the file: keep the link's name.
            reportLinkProbe(fromServer ? withTypeExtension(name, result["contentType"].toString()) : QString(),
                            // No size to show when the server did not say; error labels still explain themselves.
                            fromServer && size < 0 ? QString() : probeSize(requiresAuth, status, size).second, fromServer);
        }
        if (m_commandOperation == "browser.dismiss" && result["ok"].toBool()) m_dismissedRequest = m_browserCommandId;
        m_browserCommandId.clear();
        const bool stopped = m_commandOperation == "service.shutdown" && result["ok"].toBool();
        m_commandOperation.clear();
        emit selectionChanged();
        if (stopped) emit serviceStopped(); else request({{"type", "snapshot"}});
        // Deferred behind that snapshot, so each step sees the previous one applied.
        if (!stopped && !m_batch.isEmpty()) command(m_batch.takeFirst());
    } else invalid();
}

void ServiceDownloads::dropBatch(const QString &reason) {
    m_batch.clear();
    fail(m_error.isEmpty() ? reason : m_error + " " + reason, m_errorTarget);
}

void ServiceDownloads::disconnected() {
    m_acceptedRequest.clear(); m_acceptedIds.clear(); m_browserCommandId.clear(); m_dismissedRequest.clear();
    // Resolve every in-flight step exactly once: a deferred command was never transmitted, a
    // transmitted one has an unknown outcome, and queued batch steps are never replayed.
    const QString target = m_commandTarget;
    // A background link check that never finished is just an unanswered question, not a lost command.
    const bool probing = m_commandOperation == "download.probe" && !m_pending.isEmpty();
    if (probing) reportLinkProbe(QString(), QString(), false);
    const bool deferred = !m_deferred.isEmpty() && !probing, transmitted = !m_pending.isEmpty() && m_deferred.isEmpty() && !probing;
    QStringList notices;
    if (transmitted) {
        m_uncertain = true;
        notices << tr("Connection lost: command outcome is unknown. Inspect the refreshed list before retrying.");
    }
    if (!m_batch.isEmpty())
        notices << tr("%n further action(s) were not sent and will not be sent automatically.", nullptr, int(m_batch.size()));
    if (deferred)
        notices << tr("The command was not sent because the service connection ended. Nothing was applied, and it will not be sent automatically.");
    if (!notices.isEmpty()) {
        const QString message = notices.join(' ');
        fail(m_error.isEmpty() ? message : m_error + " " + message, target);
    }
    m_batch.clear(); m_deferred = {}; m_pending.clear(); m_commandOperation.clear(); m_commandTarget.clear();
    m_windowBaseline = true; // Every reconnect baselines hints, but still rejects same-epoch regressions.
    m_waiting = false; m_deadline.stop(); m_input.clear(); m_expected = 0;
    m_state = "disconnected"; emit viewChanged(); emit selectionChanged();
}
void ServiceDownloads::fail(const QString &message, const QString &target) {
    if (m_error != message || m_errorTarget != target) { m_error = message; m_errorTarget = target; emit errorChanged(); }
}
void ServiceDownloads::clearError() { m_uncertain = false; if (!m_error.isEmpty() || !m_errorTarget.isEmpty()) { m_error.clear(); m_errorTarget.clear(); emit errorChanged(); } }
int ServiceDownloads::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_visible.size(); }
QHash<int, QByteArray> ServiceDownloads::roleNames() const {
    return {{PreviewDownloads::StableId, "stableId"}, {PreviewDownloads::FileName, "fileName"},
        {PreviewDownloads::TransferState, "transferState"}, {PreviewDownloads::Progress, "progress"},
        {PreviewDownloads::SizeLabel, "sizeLabel"}, {PreviewDownloads::DetailLabel, "detailLabel"}, {PreviewDownloads::IconName, "iconName"}, {PreviewDownloads::DateLabel, "dateLabel"}, {PreviewDownloads::SpeedLabel, "speedLabel"}, {PreviewDownloads::EtaLabel, "etaLabel"},
        {PreviewDownloads::Selected, "isSelected"}};
}
QVariant ServiceDownloads::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.model() != this || index.column() || index.row() < 0 || index.row() >= m_visible.size()) return {};
    const auto &row = m_rows[m_visible[index.row()]];
    const qint64 size = row["size"].toString().toLongLong(), done = row["downloaded"].toString().toLongLong();
    switch (role) {
    case PreviewDownloads::StableId: return row["id"].toString();
    case PreviewDownloads::FileName: return row["name"].toString();
    case PreviewDownloads::TransferState: return row["state"].toString();
    case PreviewDownloads::Progress: return size > 0 ? std::clamp(double(done) / size, 0.0, 1.0) : 0.0;
    case PreviewDownloads::SizeLabel: return sizeText(size, sizeUnit());
    case PreviewDownloads::DetailLabel: return tr("%1 received · %2").arg(sizeText(done, sizeUnit()), row["state"].toString());
    case PreviewDownloads::SpeedLabel: {
        if (!m_capabilities.contains("monitor.stats") || row["state"] != "downloading") return QStringLiteral("—");
        const auto speed = row["speed"].toString().toLongLong();
        return speed < 0 ? QStringLiteral("—") : sizeText(speed, m_settings.value("speedUnit", "BinaryBytes").toString()) + "/s";
    }
    case PreviewDownloads::EtaLabel: {
        if (!m_capabilities.contains("monitor.stats") || row["state"] != "downloading") return QStringLiteral("—");
        return etaText(row["remainingTime"].toString().toLongLong());
    }
    case PreviewDownloads::IconName: return fileIcon(row);
    case PreviewDownloads::DateLabel: return QDateTime::fromMSecsSinceEpoch(row["dateAdded"].toString().toLongLong()).toString(QStringLiteral("dd MMM, HH:mm"));
    case PreviewDownloads::Selected: return m_selection.contains(row["id"].toString());
    default: return {};
    }
}
bool ServiceDownloads::matchesCategory(const QJsonObject &row, const QString &category) const {
    if (category.startsWith("category:")) {
        const auto members = m_categoryMembers.constFind(category);
        return members != m_categoryMembers.cend() && members->contains(row["id"].toString());
    }
    return matches(row, category);
}
QVariantMap ServiceDownloads::counts() const { return m_counts; }
void ServiceDownloads::updateCounts() {
    QVariantMap counts;
    for (const auto &key : {"all", "active", "paused", "completed", "failed", "unfinished", "compressed", "programs", "videos", "music", "pictures", "documents"}) {
        int count = 0; for (const auto &row : m_rows) if (matchesCategory(row, key)) ++count;
        counts.insert(key, count);
    }
    for (const auto &value : m_categories) {
        const auto key = "category:" + value.toMap()["id"].toString();
        int count = 0; for (const auto &row : m_rows) if (matchesCategory(row, key)) ++count;
        counts.insert(key, count);
    }
    m_counts = counts;
}
bool ServiceDownloads::sortBy(const QString &column) {
    if (!m_capabilities.contains("settings.sort") || !QStringList{"name", "size", "state", "speed", "remainingTime", "dateAdded"}.contains(column)) return false;
    return updateSettings({{"qtSortColumn", column}, {"qtSortDescending", column == sortColumn() ? !sortDescending() : column == "dateAdded"}});
}
QVariantMap ServiceDownloads::statistics() const {
    qint64 selectedBytes = 0, speed = 0; int unknown = 0, queued = 0, active = 0, activeQueues = 0;
    QSet<QString> waiting = m_manualPending;
    for (const auto &value : m_queues) {
        const auto queue = value.toMap();
        if (queue["active"].toBool()) { ++activeQueues; for (const auto &id : queue["items"].toList()) waiting.insert(id.toString()); }
    }
    auto add = [](qint64 &total, qint64 value) { total += std::min(std::max(qint64(0), value), std::numeric_limits<qint64>::max() - total); };
    for (const auto &row : m_rows) {
        const auto id = row["id"].toString(), state = row["state"].toString();
        if (m_selection.contains(id)) {
            const auto size = row["size"].toString().toLongLong();
            if (size < 0) ++unknown; else add(selectedBytes, size);
        }
        if (state == "downloading") { ++active; add(speed, row["speed"].toString().toLongLong()); }
        else if (state != "completed" && waiting.contains(id)) ++queued;
    }
    return {{"selectedBytes", QString::number(selectedBytes)}, {"unknownSizes", unknown}, {"queuedCount", queued},
        {"activeCount", active}, {"activeQueues", activeQueues},
        {"selectedSizeLabel", sizeText(selectedBytes, sizeUnit()) + (unknown ? tr(" + unknown") : QString())},
        {"speedLabel", m_capabilities.contains("monitor.stats") ? sizeText(speed, m_settings.value("speedUnit", "BinaryBytes").toString()) + "/s" : QStringLiteral("—")}};
}
void ServiceDownloads::rebuild() {
    // Cache once per model update, not once per QML sidebar binding; membership is
    // indexed by stable string ID, without repeated QVariant-list copies/scans.
    updateCounts();
    beginResetModel(); m_visible.clear();
    for (int i = 0; i < m_rows.size(); ++i)
        if (matchesCategory(m_rows[i], m_category) && m_rows[i]["name"].toString().contains(m_search.trimmed(), Qt::CaseInsensitive)) m_visible.append(i);
    const QString column = sortColumn(); const bool descending = sortDescending();
    std::sort(m_visible.begin(), m_visible.end(), [this, &column, descending](int a, int b) {
        const auto &left = m_rows[a], &right = m_rows[b]; int comparison = 0;
        if (column == "name") {
            comparison = QString::compare(left["name"].toString(), right["name"].toString(), Qt::CaseInsensitive);
        } else if (column == "state") {
            // Ascending reads like the work list: active first, finished last. Internal
            // state names are not display text, so never compare them alphabetically.
            static const QStringList order{"downloading", "queued", "paused", "failed", "completed"};
            const auto rank = [](const QJsonObject &row) { const auto i = order.indexOf(row["state"].toString()); return i < 0 ? order.size() : i; };
            comparison = rank(left) - rank(right);
        } else {
            auto number = [&column](const QJsonObject &row) {
                if ((column == "speed" || column == "remainingTime") && row["state"] != "downloading") return qint64(-1);
                bool ok = false; const auto n = row[column].toString().toLongLong(&ok); return ok ? n : qint64(-1);
            };
            const auto l = number(left), r = number(right);
            // Unknown values stay last in either direction.
            if ((l < 0) != (r < 0)) return l >= 0;
            comparison = l < r ? -1 : l > r ? 1 : 0;
        }
        if (!comparison) return left["id"].toString().toLongLong() < right["id"].toString().toLongLong();
        return descending ? comparison > 0 : comparison < 0;
    });
    // Hidden or removed rows are never actionable: drop them from the selection.
    QSet<QString> visible;
    for (int i : std::as_const(m_visible)) visible.insert(m_rows[i]["id"].toString());
    m_selection.intersect(visible);
    if (!m_selection.contains(m_selected)) m_selected = m_selection.isEmpty() ? QString() : selectedIds().constFirst();
    if (!visible.contains(m_anchor)) m_anchor = m_selected;
    endResetModel(); emit viewChanged(); emit selectionChanged();
}
void ServiceDownloads::setCategory(const QString &value) { if (counts().contains(value) && value != m_category) { m_category = value; rebuild(); } }
void ServiceDownloads::setSearch(const QString &value) { if (value.left(256) != m_search) { m_search = value.left(256); rebuild(); } }
QString ServiceDownloads::idAt(int index) const { return data(this->index(index), PreviewDownloads::StableId).toString(); }
int ServiceDownloads::indexOf(const QString &id) const {
    for (int i = 0; i < m_visible.size(); ++i) if (m_rows[m_visible[i]]["id"].toString() == id) return i;
    return -1;
}
QStringList ServiceDownloads::selectedIds() const {
    QStringList ids; // Visible order, so batches and dialogs read top to bottom.
    for (int i : m_visible) if (m_selection.contains(m_rows[i]["id"].toString())) ids.append(m_rows[i]["id"].toString());
    return ids;
}
void ServiceDownloads::setSelection(const QStringList &ids, const QString &current, const QString &anchor) {
    const QSet<QString> next(ids.cbegin(), ids.cend());
    if (next == m_selection && current == m_selected && anchor == m_anchor) return;
    const bool rowsChanged = next != m_selection;
    m_selection = next; m_selected = current; m_anchor = anchor;
    if (rowsChanged && !m_visible.isEmpty()) emit dataChanged(index(0), index(int(m_visible.size()) - 1), {PreviewDownloads::Selected});
    emit selectionChanged();
}
void ServiceDownloads::select(const QString &id) {
    if (id.isEmpty()) { setSelection({}, {}, {}); return; }
    if (indexOf(id) < 0) return;
    setSelection({id}, id, id);
}
void ServiceDownloads::toggleSelected(const QString &id) {
    if (indexOf(id) < 0) return;
    QStringList ids = selectedIds();
    if (ids.removeAll(id)) setSelection(ids, ids.isEmpty() ? QString() : ids.constLast(), id);
    else setSelection(ids << id, id, id);
}
void ServiceDownloads::selectRange(const QString &id, bool extend) {
    const int to = indexOf(id);
    if (to < 0) return;
    const int from = indexOf(m_anchor) >= 0 ? indexOf(m_anchor) : to;
    QStringList ids = extend ? selectedIds() : QStringList();
    for (int i = qMin(from, to); i <= qMax(from, to); ++i) if (!ids.contains(idAt(i))) ids.append(idAt(i));
    setSelection(ids, id, from == to ? id : m_anchor);
}
void ServiceDownloads::selectAll() {
    QStringList ids;
    for (int i : std::as_const(m_visible)) ids.append(m_rows[i]["id"].toString());
    setSelection(ids, m_selection.contains(m_selected) ? m_selected : ids.value(0), m_anchor.isEmpty() ? ids.value(0) : m_anchor);
}
QJsonObject ServiceDownloads::selected() const { for (const auto &row : m_rows) if (row["id"].toString() == m_selected) return row; return {}; }
QString ServiceDownloads::selectedName() const { return selected()["name"].toString(); }
bool ServiceDownloads::canPause() const {
    return m_state == "ready" && m_pending.isEmpty() && std::any_of(m_rows.cbegin(), m_rows.cend(), [this](const QJsonObject &row) {
        return m_selection.contains(row["id"].toString()) && QStringList{"downloading", "queued"}.contains(row["state"].toString()); });
}
bool ServiceDownloads::canResume() const {
    return m_state == "ready" && m_pending.isEmpty() && std::any_of(m_rows.cbegin(), m_rows.cend(), [this](const QJsonObject &row) {
        return m_selection.contains(row["id"].toString()) && QStringList{"paused", "failed", "queued"}.contains(row["state"].toString()); });
}
bool ServiceDownloads::command(QJsonObject cmd) {
    if (m_state != "ready" || !m_pending.isEmpty()) { fail(tr("Service busy or disconnected. Try again after synchronization.")); return false; }
    if (!m_capabilities.contains(cmd["op"].toString())) { fail(tr("This service does not support that operation.")); return false; }
    m_commandOperation = cmd["op"].toString();
    m_commandTarget = cmd.value("id").toString();
    if (m_commandOperation == "browser.probe") { m_probeRequest = cmd["requestId"].toString(); m_probeIndex = cmd["index"].toInt(-1); }
    m_pending = QUuid::createUuid().toString(QUuid::WithoutBraces);
    cmd["type"] = "command"; cmd["epoch"] = m_epoch; cmd["commandId"] = m_pending;
    // One request in flight: a routine snapshot defers, never rejects, a user command.
    if (m_waiting) m_deferred = cmd; else request(cmd);
    emit selectionChanged(); return true;
}
bool ServiceDownloads::commandBatch(const QList<QJsonObject> &commands) {
    if (commands.isEmpty()) return false;
    if (!m_batch.isEmpty()) { fail(tr("Service busy or disconnected. Try again after synchronization.")); return false; }
    for (const auto &cmd : commands)
        if (!m_capabilities.contains(cmd["op"].toString())) { fail(tr("This service does not support that operation.")); return false; }
    if (!command(commands.constFirst())) return false;
    m_batch = commands.mid(1);
    return true;
}
bool ServiceDownloads::watchDownloadParts(const QString &id, bool watch) {
    if (watch) {
        if (!numericId(id) || (!m_partWatches.contains(id) && m_partWatches.size() >= 16)) return false;
        ++m_partWatches[id];
    } else if (m_partWatches.contains(id) && --m_partWatches[id] <= 0) m_partWatches.remove(id);
    return true;
}
QString ServiceDownloads::downloadName(const QString &id) const {
    for (const auto &row : m_rows) if (row["id"].toString() == id) return row["name"].toString();
    return tr("Unavailable download (%1)").arg(id);
}
QVariantMap ServiceDownloads::downloadDetails(const QString &id) const {
    if (!m_capabilities.contains("download.details")) return {};
    for (const auto &row : m_rows) if (row["id"].toString() == id) return row.toVariantMap();
    return {};
}
bool ServiceDownloads::resumeDownload(const QString &id) {
    for (const auto &row : m_rows) if (row["id"].toString() == id
        && QStringList{"paused", "failed", "queued"}.contains(row["state"].toString()))
        return command({{"op", "resume"}, {"id", id}});
    return false;
}
bool ServiceDownloads::pauseDownload(const QString &id) {
    for (const auto &row : m_rows) if (row["id"].toString() == id
        && QStringList{"downloading", "queued"}.contains(row["state"].toString()))
        return command({{"op", "pause"}, {"id", id}});
    return false;
}
QVariantMap ServiceDownloads::downloadState(const QString &id) const {
    for (const auto &row : m_rows) {
        if (row["id"].toString() != id) continue;
        const QString state = row["state"].toString();
        const qint64 size = row["size"].toString().toLongLong(), done = row["downloaded"].toString().toLongLong();
        const bool stats = m_capabilities.contains("monitor.stats") && state == "downloading";
        const qint64 speed = stats ? row["speed"].toString().toLongLong() : -1;
        const qint64 eta = stats ? row["remainingTime"].toString().toLongLong() : -1;
        QVariantMap result{{"available", true}, {"id", id}, {"name", row["name"].toString()}, {"state", state},
            {"waiting", state == "queued" && m_manualPending.contains(id)},
            {"size", row["size"].toString()}, {"downloaded", row["downloaded"].toString()},
            // -1: unknown total size. Never estimated from elapsed time.
            {"progress", size > 0 ? std::clamp(double(done) / size, 0.0, 1.0) : -1.0},
            {"sizeLabel", size < 0 ? tr("Unknown") : sizeText(size, sizeUnit())}, {"downloadedLabel", sizeText(done, sizeUnit())},
            {"speedLabel", speed > 0 ? sizeText(speed, m_settings.value("speedUnit", "BinaryBytes").toString()) + "/s" : QString()},
            {"etaLabel", eta >= 0 ? etaText(eta) : QString()}, {"iconName", fileIcon(row)}};
        if (m_capabilities.contains("download.details")) {
            const auto details = row["details"].toObject();
            result.insert("url", details["url"].toString()); result.insert("folder", details["folder"].toString());
            if (m_capabilities.contains("download.checksum")) result.insert("fileChecksum", details["fileChecksum"].toString());
            result.insert("error", details["error"].isObject() ? details["error"].toObject().toVariantMap() : QVariant());
            if (m_capabilities.contains("download.http-options")) result.insert("hasCredentials", details["hasCredentials"].toBool());
        }
        return result;
    }
    return {{"available", false}, {"id", id}};
}
QUrl ServiceDownloads::completedFileUrl(const QString &id, bool folder) const {
    // Opening is an explicit user action in the installed runtime only; fixtures never launch apps.
    if (m_runtime.value("mode") != "explicit-runtime" || !m_capabilities.contains("download.details")) return {};
    for (const auto &row : m_rows) {
        if (row["id"].toString() != id || row["state"].toString() != "completed") continue;
        const QString path = completedFilePath(row);
        if (path.isEmpty()) return {};
        return QUrl::fromLocalFile(folder ? QFileInfo(path).absolutePath() : path);
    }
    return {};
}
QVariantList ServiceDownloads::findDuplicates(const QString &url, const QString &name, const QString &folder) const {
    // The service compares exact stored values (link, folder + name); so do we.
    QVariantList result;
    if (!m_capabilities.contains("download.details")) return result;
    const QString rawLink = url.trimmed(), file = name.trimmed(), target = folder.trimmed();
    // Add stores QUrl::toString(), so match both the typed text and what Add would store.
    const QUrl parsed(rawLink, QUrl::StrictMode);
    const QString link = parsed.isValid() && !parsed.host().isEmpty() ? parsed.toString() : rawLink;
    // A relative folder can never be the absolute destination the service records.
    const bool comparableFolder = QDir::isAbsolutePath(target);
    for (const auto &row : m_rows) {
        const auto details = row["details"].toObject();
        const auto storedLink = details["url"].toString();
        const bool sameUrl = !rawLink.isEmpty() && (storedLink == link || storedLink == rawLink);
        const bool sameDestination = comparableFolder && !file.isEmpty() && row["name"].toString() == file
            && QDir::cleanPath(details["folder"].toString()) == QDir::cleanPath(target);
        if (!sameUrl && !sameDestination) continue;
        const QString id = row["id"].toString();
        result.append(QVariantMap{{"id", id}, {"name", row["name"].toString()}, {"state", row["state"].toString()},
            {"sameUrl", sameUrl}, {"sameDestination", sameDestination}, {"fileUrl", completedFileUrl(id, false)}});
        if (result.size() >= 100) break;
    }
    return result;
}
bool ServiceDownloads::destinationExists(const QString &folder, const QString &name) const {
    // Same name rules as Add: a name Add would reject is never reported as an existing file.
    const QString file = name.trimmed();
    if (!QDir::isAbsolutePath(folder) || file.isEmpty() || file.size() > 180
        || file == "." || file == ".." || file.contains('/') || file.contains('\\')
        || std::any_of(file.cbegin(), file.cend(), [](QChar c) { return c.isNonCharacter() || c.category() == QChar::Other_Control; })) return false;
    const QFileInfo info(QDir(folder).filePath(file));
    // A dangling link still occupies the name and would make Add collide.
    return info.exists() || info.isSymLink();
}
bool ServiceDownloads::canForgetDownloads(const QStringList &ids) const {
    return !ids.isEmpty() && std::all_of(ids.cbegin(), ids.cend(), [this](const QString &id) { return canForgetDownload(id); });
}
bool ServiceDownloads::forgetDownloads(const QStringList &ids, bool deleteFiles) {
    if (!canForgetDownloads(ids)) return false;
    QList<QJsonObject> commands;
    for (const auto &id : ids) commands.append({{"op", "download.forget"}, {"id", id}, {"keepFiles", !deleteFiles}});
    return commandBatch(commands);
}
bool ServiceDownloads::canForgetDownload(const QString &id) const {
    if (busy() || !m_capabilities.contains("download.forget")) return false;
    for (const auto &row : m_rows) if (row["id"].toString() == id) {
        if (row["state"] == "downloading") return false;
        for (const auto &entry : m_queues) {
            const auto queue = entry.toMap();
            if (queue["active"].toBool() && queue["items"].toList().contains(id)) return false;
        }
        return true;
    }
    return false;
}
bool ServiceDownloads::forgetDownload(const QString &id, bool deleteFiles) {
    return canForgetDownload(id) && command({{"op", "download.forget"}, {"id", id}, {"keepFiles", !deleteFiles}});
}
bool ServiceDownloads::updateSettings(const QVariantMap &settings) {
    return command({{"op", "settings.update"}, {"settings", QJsonObject::fromVariantMap(settings)}});
}
bool ServiceDownloads::canSetDefaultFolder(const QString &folder) const {
    if (!m_capabilities.contains("settings.folder")) return false;
    const QString path = QDir::cleanPath(folder.trimmed());
    if (!QDir::isAbsolutePath(path) || path == QLatin1String("/")) return false;
    // The service refuses symlinked runtime paths and requires an existing writable folder;
    // checking here turns a rejected round-trip into a clear local answer.
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink()) return false;
    for (const auto &value : m_runtime.value("destinationRoots").toStringList()) {
        const QString root = QDir::cleanPath(value);
        if (root.isEmpty() || root == QLatin1String("/")) continue;
        if (path == root || path.startsWith(root + '/')) return true;
    }
    return false;
}
bool ServiceDownloads::setDefaultFolder(const QString &folder) {
    if (!m_capabilities.contains("settings.folder")) {
        fail(tr("This service does not support choosing a default download folder.")); return false;
    }
    // Empty resets to the runtime destination, which is how the original stores "not set".
    if (folder.trimmed().isEmpty()) return updateSettings({{"defaultDownloadFolder", QString()}});
    const QString path = QDir::cleanPath(folder.trimmed());
    if (!canSetDefaultFolder(path)) {
        fail(tr("Choose an existing folder inside the approved destination roots.")); return false;
    }
    return updateSettings({{"defaultDownloadFolder", path}});
}
bool ServiceDownloads::queueCommand(const QString &operation, const QString &queueId, const QVariantMap &arguments) {
    if (!operation.startsWith("queue.")) return false;
    auto request = QJsonObject::fromVariantMap(arguments);
    request["op"] = operation; request["queueId"] = queueId;
    return command(request);
}
bool ServiceDownloads::categoryCommand(const QString &operation, const QString &categoryId, const QVariantMap &arguments) {
    if (!operation.startsWith("category.")) return false;
    auto request = QJsonObject::fromVariantMap(arguments);
    request["op"] = operation; request["categoryId"] = categoryId;
    return command(request);
}
bool ServiceDownloads::pauseSelected() {
    if (!canPause()) return false;
    QList<QJsonObject> commands;
    for (const auto &id : selectedIds()) if (QStringList{"downloading", "queued"}.contains(downloadState(id)["state"].toString())) commands.append({{"op", "pause"}, {"id", id}});
    return commandBatch(commands);
}
bool ServiceDownloads::resumeSelected() {
    if (!canResume()) return false;
    QList<QJsonObject> commands;
    for (const auto &id : selectedIds()) if (QStringList{"paused", "failed", "queued"}.contains(downloadState(id)["state"].toString())) commands.append({{"op", "resume"}, {"id", id}});
    return commandBatch(commands);
}
bool ServiceDownloads::addToQueue(const QString &queueId, const QStringList &ids) {
    QList<QJsonObject> commands;
    for (const auto &value : m_queues) {
        const auto queue = value.toMap();
        if (queue["id"].toString() != queueId) continue;
        const auto members = queue["items"].toList();
        for (const auto &id : ids) if (numericId(id) && !members.contains(id)) commands.append({{"op", "queue.add"}, {"queueId", queueId}, {"id", id}});
    }
    return commandBatch(commands);
}
bool ServiceDownloads::assignCategory(const QString &categoryId, const QStringList &ids) {
    QList<QJsonObject> commands;
    for (const auto &id : ids) if (numericId(id)) commands.append({{"op", "category.assign"}, {"categoryId", categoryId}, {"id", id}});
    return commandBatch(commands);
}
bool ServiceDownloads::addExample(const QString &url, const QString &name) { return addDownload(url, name, false); }
bool ServiceDownloads::addDownload(const QString &url, const QString &name, bool start) {
    return addCategorizedDownload(url, name, start, "auto");
}
bool ServiceDownloads::canMutateDownload(const QString &id, const QString &operation) const {
    if (busy() || !QStringList{"download.reset", "download.move"}.contains(operation) || !m_capabilities.contains(operation)) return false;
    for (const auto &row : m_rows) if (row["id"].toString() == id) {
        if (row["state"] == "downloading") return false;
        for (const auto &value : m_queues) {
            const auto queue = value.toMap();
            if (queue["active"].toBool() && queue["items"].toList().contains(id)) return false;
        }
        return true;
    }
    return false;
}
bool ServiceDownloads::resetDownload(const QString &id) {
    return canMutateDownload(id, "download.reset") && command({{"op", "download.reset"}, {"id", id}});
}
bool ServiceDownloads::moveDownload(const QString &id, const QString &name, const QString &folder) {
    if (!canMutateDownload(id, "download.move")) return false;
    return command({{"op", "download.move"}, {"id", id}, {"name", name.trimmed()}, {"folder", folder.trimmed()}});
}
bool ServiceDownloads::copyDownloadLinks() {
    if (!m_capabilities.contains("download.links")) return false;
    QStringList links;
    for (const auto &id : selectedIds()) {
        const auto link = downloadDetails(id).value("details").toMap().value("url").toString();
        const QUrl url(link, QUrl::StrictMode);
        if (url.isValid() && QStringList{"http", "https"}.contains(url.scheme()) && !url.host().isEmpty() && url.userInfo().isEmpty()) links.append(link);
    }
    if (links.isEmpty()) return false;
    QGuiApplication::clipboard()->setText(links.join('\n'));
    return true;
}
QUrl ServiceDownloads::downloadPageUrl(const QString &id) const {
    if (!m_capabilities.contains("download.links")) return {};
    const auto page = downloadDetails(id).value("details").toMap().value("downloadPage").toString();
    const QUrl url(page, QUrl::StrictMode);
    return url.isValid() && QStringList{"http", "https"}.contains(url.scheme()) && !url.host().isEmpty() && url.userInfo().isEmpty() ? url : QUrl();
}
bool ServiceDownloads::editDownload(const QString &id, const QVariantMap &options) {
    const auto row = downloadDetails(id);
    if (row.isEmpty() || row["state"] == "completed" || row["state"] == "downloading") return false;
    auto request = QJsonObject::fromVariantMap(options);
    request["op"] = "download.edit"; request["id"] = id;
    return command(request);
}
bool ServiceDownloads::addCategorizedDownload(const QString &url, const QString &name, bool start, const QString &categoryId) {
    return addConfiguredDownload(url, name, start, categoryId, {});
}
QString ServiceDownloads::suggestedName(const QString &url) const {
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    if (!parsed.isValid() || !QStringList{"http", "https"}.contains(parsed.scheme()) || parsed.host().isEmpty()) return {};
    return cleanFileName(parsed.path(QUrl::FullyDecoded).section('/', -1));
}
void ServiceDownloads::reportLinkProbe(const QString &name, const QString &sizeLabel, bool fromServer) {
    QMetaObject::invokeMethod(this, [this, url = m_linkProbeUrl, name, sizeLabel, fromServer] { emit linkProbed(url, name, sizeLabel, fromServer); },
                              Qt::QueuedConnection);
}
bool ServiceDownloads::probeLink(const QString &url, const QVariantMap &options) {
    // Silent refusals: this runs in the background while the user is still typing.
    QUrl parsed;
    if (m_state != "ready" || !m_pending.isEmpty() || !m_capabilities.contains("download.probe") || !acceptableLink(url, &parsed)) return false;
    auto request = QJsonObject::fromVariantMap(options);
    request.remove("duplicatePolicy"); request.remove("folder");
    request["op"] = "download.probe"; request["url"] = parsed.toString();
    if (!command(request)) return false;
    m_linkProbeUrl = url;
    return true;
}
bool ServiceDownloads::acceptableLink(const QString &url, QUrl *parsedOut) const {
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    const bool isolated = m_runtime.value("mode", "isolated") != "explicit-runtime";
    if (url.size() > 2048 || !parsed.isValid() || !QStringList{"http", "https"}.contains(parsed.scheme()) || parsed.host().isEmpty()
        || !parsed.userInfo().isEmpty() || parsed.hasFragment() || parsed.port() == 15151
        || (isolated && (parsed.scheme() != "http" || parsed.host() != "127.0.0.1" || parsed.port() < 1 || parsed.port() > 65535))) return false;
    if (parsedOut) *parsedOut = parsed;
    return true;
}
bool ServiceDownloads::addConfiguredDownload(const QString &url, const QString &name, bool start, const QString &categoryId, const QVariantMap &options) {
    QUrl parsed;
    if (!acceptableLink(url, &parsed)) {
        const bool isolated = m_runtime.value("mode", "isolated") != "explicit-runtime";
        fail(isolated ? tr("Enter a loopback HTTP fixture URL with an explicit port, not the production integration port.")
                      : tr("Enter an HTTP(S) URL without embedded credentials or fragments. Port 15151 is forbidden.")); return false;
    }
    const QString boundedName = name.trimmed();
    if (boundedName.isEmpty() || boundedName.size() > 180 || boundedName == "." || boundedName == ".."
        || boundedName.contains('/') || boundedName.contains('\\')
        || std::any_of(boundedName.cbegin(), boundedName.cend(), [](QChar c) { return c.isNonCharacter() || c.category() == QChar::Other_Control; })) {
        fail(tr("Enter a file name of 1–180 characters without path separators or control characters.")); return false;
    }
    auto request = QJsonObject::fromVariantMap(options);
    // Overwrite is only ever an explicit per-add choice, and only for a service that replaces
    // existing records/destinations instead of numbering them. An empty policy is omitted so
    // the service default (numbered) applies instead of being rejected as unsupported.
    const auto policyValue = request.take("duplicatePolicy");
    if (!policyValue.isUndefined() && !policyValue.isNull()) {
        const QString policy = policyValue.isString() ? policyValue.toString() : QString();
        if (!QStringList{"reject", "numbered", "overwrite"}.contains(policy)) {
            fail(tr("Unsupported duplicate policy.")); return false;
        }
        if (policy == "overwrite" && !m_capabilities.contains("download.replace")) {
            fail(tr("This service cannot replace an existing download.")); return false;
        }
        request["duplicatePolicy"] = policy;
    }
    request["op"] = "add"; request["url"] = parsed.toString(); request["name"] = boundedName;
    request["start"] = start; request["categoryId"] = categoryId;
    return command(request);
}
bool ServiceDownloads::acceptBrowserRequest(const QString &requestId, const QVariantList &items, bool start, const QString &categoryId,
                                            const QString &folder, const QString &duplicatePolicy) {
    // The service re-checks every index/URL against the pending request before adding anything.
    for (const auto &value : m_browserRequests) {
        const auto pending = value.toMap();
        if (pending["id"].toString() == requestId && pending["outcomeUnknown"].toBool()) {
            fail(tr("Previous acceptance outcome is unknown. Inspect downloads; this request cannot be replayed.")); return false;
        }
    }
    QJsonArray chosen;
    for (const auto &value : items) {
        const auto item = value.toMap(); const QString name = item.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 180 || name == "." || name == ".." || name.contains('/') || name.contains('\\')) {
            fail(tr("Enter a file name of 1–180 characters without path separators.")); return false;
        }
        chosen.append(QJsonObject{{"index", item.value("index").toInt()}, {"url", item.value("url").toString()}, {"name", name}});
    }
    if (chosen.isEmpty()) { fail(tr("Select at least one link to download.")); return false; }
    if (!duplicatePolicy.isEmpty() && !QStringList{"reject", "numbered", "overwrite"}.contains(duplicatePolicy)) {
        fail(tr("Unsupported duplicate policy.")); return false;
    }
    if (duplicatePolicy == "overwrite" && !m_capabilities.contains("download.replace")) {
        fail(tr("This service cannot replace an existing download.")); return false;
    }
    QJsonObject request{{"op", "browser.accept"}, {"requestId", requestId}, {"items", chosen}, {"start", start}, {"categoryId", categoryId}};
    if (!folder.isEmpty()) request["folder"] = folder;
    if (!duplicatePolicy.isEmpty()) request["duplicatePolicy"] = duplicatePolicy;
    if (!command(request)) return false;
    m_browserCommandId = requestId; m_browserCommandStart = start;
    return true;
}
bool ServiceDownloads::dismissBrowserRequest(const QString &requestId) {
    if (!command({{"op", "browser.dismiss"}, {"requestId", requestId}})) return false;
    m_browserCommandId = requestId;
    return true;
}
bool ServiceDownloads::updateBrowserSettings(const QVariantMap &settings) {
    return command({{"op", "browser.settings"}, {"settings", QJsonObject::fromVariantMap(settings)}});
}
bool ServiceDownloads::updateIntegration(const QVariantMap &settings) {
    return command({{"op", "settings.integration"}, {"settings", QJsonObject::fromVariantMap(settings)}});
}
bool ServiceDownloads::importLinks(const QString &text) {
    if (text.size() > 256 * 1024) { fail(tr("Clipboard text is too large.")); return false; }
    return command({{"op", "links.import"}, {"text", text}});
}
bool ServiceDownloads::reviewLinks(const QString &text) {
    if (text.size() > 256 * 1024) { fail(tr("Link text is too large.")); return false; }
    return command({{"op", "links.review"}, {"text", text}});
}
QPair<QString, QString> ServiceDownloads::probeSize(bool requiresAuth, int status, qint64 size) const {
    // A 404/403 page's Content-Length is not the download's size.
    if (requiresAuth) return {QStringLiteral("-1"), tr("Login required")};
    if (status < 200 || status >= 300) return {QStringLiteral("-1"), tr("Unavailable")};
    return {QString::number(size), sizeText(size, sizeUnit())};
}
bool ServiceDownloads::probeBrowserItems(const QString &requestId, const QVariantList &indices) {
    const bool batch = m_capabilities.contains("browser.probeBatch");
    if ((!batch && !m_capabilities.contains("browser.probe")) || indices.isEmpty() || indices.size() > 100) return false;
    QList<QJsonObject> commands; QSet<int> seen; QJsonArray all;
    for (const auto &value : indices) {
        bool ok = false; const int index = value.toInt(&ok);
        if (!ok || index < 0 || index >= 100 || seen.contains(index)) return false;
        seen.insert(index); all.append(index); commands.append({{"op", "browser.probe"}, {"requestId", requestId}, {"index", index}});
    }
    // One immediate command; results arrive in snapshots without holding the command slot.
    if (batch) return command({{"op", "browser.probeBatch"}, {"requestId", requestId}, {"indices", all}});
    // Older services: one bounded probe at a time through the command slot.
    return commandBatch(commands);
}
bool ServiceDownloads::checksumCommand(const QString &id, const QString &action, const QString &algorithm, const QString &expected) {
    if (!numericId(id) || !QStringList{"start", "save", "cancel"}.contains(action)) return false;
    if (action != "cancel" && downloadState(id).value("state").toString() != "completed") return false;
    if (action != "cancel" && !QStringList{"MD5", "SHA-1", "SHA-256", "SHA-512"}.contains(algorithm)) return false;
    return command({{"op", "download.checksum"}, {"id", id}, {"action", action}, {"algorithm", algorithm},
        {"expected", expected.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(expected)}});
}
bool ServiceDownloads::taskCommand(const QString &operation) {
    return QStringList{"queue.startAll", "queue.stopAll", "download.stopAll"}.contains(operation) && command({{"op", operation}});
}
bool ServiceDownloads::shutdownService() { return command({{"op", "service.shutdown"}}); }
bool ServiceDownloads::answerPowerAction(const QString &serial, bool execute) {
    if (serial.isEmpty() || m_powerAction.value("serial").toString() != serial) return false;
    return command({{"op", execute ? "power.execute" : "power.cancel"}, {"serial", serial}});
}
