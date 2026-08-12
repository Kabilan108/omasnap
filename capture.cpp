#include "capture.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

bool loadCaptureFonts() {
  static const int fontId =
      QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Neucha.ttf"));
  return fontId >= 0;
}

QFont annotationTextFont(qreal size) {
  static_cast<void>(loadCaptureFonts());
  QFont font(QStringLiteral("Neucha"));
  font.setWeight(QFont::Normal);
  font.setItalic(false);
  font.setPixelSize(qRound(std::max<qreal>(18.0, size * 5.0)));
  return font;
}

bool ensurePrivateDirectory(const QString &path) {
  if (path.isEmpty())
    return false;

  const QString cleanPath = QDir::cleanPath(path);
  const QByteArray encoded = QFile::encodeName(cleanPath);
  const int fd = ::open(encoded.constData(),
                        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd >= 0) {
    struct stat info{};
    const bool ownedDirectory = ::fstat(fd, &info) == 0 &&
                                S_ISDIR(info.st_mode) &&
                                info.st_uid == ::geteuid();
    const bool secured = ownedDirectory && ::fchmod(fd, S_IRWXU) == 0;
    ::close(fd);
    return secured;
  }
  if (errno != ENOENT)
    return false;

  const QString parent = QFileInfo(cleanPath).dir().absolutePath();
  if (parent.isEmpty() || parent == cleanPath)
    return false;
  if (!QFileInfo(parent).isDir() && !ensurePrivateDirectory(parent))
    return false;

  if (::mkdir(encoded.constData(), S_IRWXU) == 0)
    return true;
  return errno == EEXIST && ensurePrivateDirectory(cleanPath);
}

QString secureRuntimeDirectory() {
  QString runtime =
      QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  if (runtime.isEmpty()) {
    runtime = QDir(QDir::tempPath())
                  .filePath(QStringLiteral("omasnap-%1").arg(::getuid()));
  } else {
    runtime = QDir(runtime).filePath(QStringLiteral("omasnap"));
  }
  return ensurePrivateDirectory(runtime) ? QDir::cleanPath(runtime) : QString();
}

namespace {
struct ProcessResult {
  QByteArray output;
  QByteArray error;
  int exitCode = -1;
  bool finished = false;
};

struct ClipboardSnapshot {
  QByteArray payload;
  QString mimeType;
  bool wasEmpty = false;
  bool valid = false;
};

ProcessResult runProcess(const QString &program, const QStringList &arguments,
                         const QByteArray &input = {}, int timeoutMs = 10000) {
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(program, arguments);
  if (!process.waitForStarted(2000))
    return {{}, process.errorString().toUtf8(), -1, false};

  if (!input.isEmpty())
    process.write(input);
  process.closeWriteChannel();
  const bool finished = process.waitForFinished(timeoutMs);
  if (!finished)
    process.kill();
  return {process.readAllStandardOutput(), process.readAllStandardError(),
          finished ? process.exitCode() : -1, finished};
}

QString processFailure(const ProcessResult &result) {
  const QString stderrText = QString::fromUtf8(result.error).trimmed();
  return stderrText.isEmpty() ? QStringLiteral("command did not complete")
                              : stderrText;
}

ClipboardSnapshot snapshotClipboard() {
  const ProcessResult listed = runProcess(
      QStringLiteral("wl-paste"), {QStringLiteral("--list-types")}, {}, 2000);
  if (listed.finished && listed.exitCode != 0) {
    ClipboardSnapshot snapshot;
    snapshot.wasEmpty = true;
    snapshot.valid = true;
    return snapshot;
  }
  if (!listed.finished)
    return {};

  const QStringList types =
      QString::fromUtf8(listed.output).split('\n', Qt::SkipEmptyParts);
  if (types.isEmpty()) {
    ClipboardSnapshot snapshot;
    snapshot.wasEmpty = true;
    snapshot.valid = true;
    return snapshot;
  }

  static const QStringList preferredTypes{
      QStringLiteral("image/png"),
      QStringLiteral("text/plain;charset=utf-8"),
      QStringLiteral("text/plain"),
      QStringLiteral("UTF8_STRING"),
  };
  QString mimeType;
  for (const QString &preferred : preferredTypes) {
    if (types.contains(preferred)) {
      mimeType = preferred;
      break;
    }
  }
  if (mimeType.isEmpty())
    mimeType = types.first();

  const ProcessResult pasted = runProcess(
      QStringLiteral("wl-paste"),
      {QStringLiteral("--no-newline"), QStringLiteral("--type"), mimeType}, {},
      5000);
  if (!pasted.finished || pasted.exitCode != 0)
    return {};
  return {
      .payload = pasted.output, .mimeType = std::move(mimeType), .valid = true};
}

bool restoreClipboard(const ClipboardSnapshot &snapshot, QString &error) {
  if (!snapshot.valid)
    return true;
  const ProcessResult restored =
      snapshot.wasEmpty
          ? runProcess(QStringLiteral("wl-copy"), {QStringLiteral("--clear")},
                       {}, 5000)
          : runProcess(QStringLiteral("wl-copy"),
                       {QStringLiteral("--type"), snapshot.mimeType},
                       snapshot.payload, 5000);
  if (restored.finished && restored.exitCode == 0)
    return true;
  error = QStringLiteral("Could not restore clipboard after window capture: %1")
              .arg(processFailure(restored));
  return false;
}

void waitForClipboardImage(const QByteArray &png) {
  if (png.isEmpty())
    return;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 2000) {
    const ProcessResult pasted =
        runProcess(QStringLiteral("wl-paste"),
                   {QStringLiteral("--no-newline"), QStringLiteral("--type"),
                    QStringLiteral("image/png")},
                   {}, 500);
    if (pasted.finished && pasted.exitCode == 0 && pasted.output == png)
      return;
    QThread::msleep(20);
  }
}

bool copyToWaylandClipboard(const QString &mimeType, const QByteArray &payload,
                            QString &error) {
  QByteArray lastError;
  for (int attempt = 0; attempt < 2; ++attempt) {
    const ProcessResult copied =
        runProcess(QStringLiteral("wl-copy"),
                   {QStringLiteral("--type"), mimeType}, payload, 5000);
    if (!copied.finished || copied.exitCode != 0) {
      lastError = copied.error;
      continue;
    }

    const ProcessResult verified = runProcess(
        QStringLiteral("wl-paste"),
        {QStringLiteral("--no-newline"), QStringLiteral("--type"), mimeType},
        {}, 5000);
    if (verified.finished && verified.exitCode == 0 &&
        verified.output == payload)
      return true;
    lastError = verified.error;
    if (lastError.isEmpty())
      lastError = QByteArrayLiteral("clipboard verification did not match");
  }
  error = QStringLiteral("Could not persist clipboard: %1")
              .arg(QString::fromUtf8(lastError).trimmed());
  return false;
}

QString runtimePath(const QString &name) {
  const QString runtime = secureRuntimeDirectory();
  return runtime.isEmpty() ? QString() : QDir(runtime).filePath(name);
}

QString screenshotTargetPath(QString &error) {
  QString root = qEnvironmentVariable("OMASNAP_SCREENSHOT_DIR");
  if (root.isEmpty())
    root =
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("Screenshots"));
  if (!QDir().mkpath(root)) {
    error =
        QStringLiteral("Could not create screenshot directory: %1").arg(root);
    return {};
  }

  const QString stem = QStringLiteral("screenshot-%1")
                           .arg(QDateTime::currentDateTime().toString(
                               QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
  QString path = QDir(root).filePath(stem + QStringLiteral(".png"));
  for (int suffix = 2; QFile::exists(path); ++suffix)
    path =
        QDir(root).filePath(QStringLiteral("%1-%2.png").arg(stem).arg(suffix));
  return path;
}

bool parseMonitorObject(const QJsonObject &object, qint64 workspaceId,
                        MonitorInfo &monitor, QString &error) {
  const QJsonObject logical =
      object.value(QStringLiteral("logical")).toObject();
  const QString name = object.value(QStringLiteral("name")).toString();
  const int width = logical.value(QStringLiteral("width")).toInt();
  const int height = logical.value(QStringLiteral("height")).toInt();
  const qreal scale = logical.value(QStringLiteral("scale")).toDouble(1.0);
  if (name.isEmpty() || width <= 0 || height <= 0 || scale <= 0) {
    error = QStringLiteral("Niri reported an invalid logical output");
    return false;
  }

  monitor.name = name;
  monitor.geometry = {logical.value(QStringLiteral("x")).toInt(),
                      logical.value(QStringLiteral("y")).toInt(), width,
                      height};
  monitor.pixelSize = {std::max(1, qRound(width * scale)),
                       std::max(1, qRound(height * scale))};
  monitor.scale = scale;
  monitor.workspaceId = workspaceId;
  return true;
}

bool focusedWorkspaceId(const QByteArray &json, const QString &outputName,
                        qint64 &workspaceId, QString &error) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    error = QStringLiteral("Could not parse Niri workspaces: %1")
                .arg(parseError.errorString());
    return false;
  }

  for (const QJsonValue value : document.array()) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("output")).toString() != outputName ||
        !object.value(QStringLiteral("is_focused")).toBool())
      continue;
    workspaceId = object.value(QStringLiteral("id")).toInteger(-1);
    return workspaceId >= 0;
  }

  for (const QJsonValue value : document.array()) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("output")).toString() != outputName ||
        !object.value(QStringLiteral("is_active")).toBool())
      continue;
    workspaceId = object.value(QStringLiteral("id")).toInteger(-1);
    return workspaceId >= 0;
  }

  error = QStringLiteral("Niri did not report an active workspace on %1")
              .arg(outputName);
  return false;
}

bool parseFocusedMonitor(const QByteArray &outputJson,
                         const QByteArray &workspacesJson, MonitorInfo &monitor,
                         QString &error) {
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(outputJson, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    error = QStringLiteral("Could not parse Niri focused output: %1")
                .arg(parseError.errorString());
    return false;
  }

  const QJsonObject output = document.object();
  qint64 workspaceId = -1;
  if (!focusedWorkspaceId(workspacesJson,
                          output.value(QStringLiteral("name")).toString(),
                          workspaceId, error))
    return false;
  return parseMonitorObject(output, workspaceId, monitor, error);
}

bool parsePickedWindow(const QByteArray &json, QString &windowId,
                       qint64 &workspaceId, QString &error) {
  if (json.trimmed() == QByteArrayLiteral("null")) {
    error = QStringLiteral("Window selection was cancelled");
    return false;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    error = QStringLiteral("Could not parse Niri window selection: %1")
                .arg(parseError.errorString());
    return false;
  }
  if (!document.isObject()) {
    error = QStringLiteral("Niri returned an invalid window selection");
    return false;
  }

  const QJsonObject object = document.object();
  const qint64 id = object.value(QStringLiteral("id")).toInteger(-1);
  workspaceId = object.value(QStringLiteral("workspace_id")).toInteger(-1);
  if (id < 0 || workspaceId < 0) {
    error = QStringLiteral("Niri returned incomplete window information");
    return false;
  }

  windowId = QString::number(id);
  return true;
}

bool parseMonitorForWorkspace(const QByteArray &outputsJson,
                              const QByteArray &workspacesJson,
                              qint64 workspaceId, MonitorInfo &monitor,
                              QString &error) {
  QJsonParseError workspaceParseError;
  const QJsonDocument workspaces =
      QJsonDocument::fromJson(workspacesJson, &workspaceParseError);
  if (workspaceParseError.error != QJsonParseError::NoError ||
      !workspaces.isArray()) {
    error = QStringLiteral("Could not parse Niri workspaces: %1")
                .arg(workspaceParseError.errorString());
    return false;
  }

  QString outputName;
  for (const QJsonValue value : workspaces.array()) {
    const QJsonObject workspace = value.toObject();
    if (workspace.value(QStringLiteral("id")).toInteger(-1) == workspaceId) {
      outputName = workspace.value(QStringLiteral("output")).toString();
      break;
    }
  }
  if (outputName.isEmpty()) {
    error = QStringLiteral("Selected window is not on a Niri output");
    return false;
  }

  QJsonParseError outputParseError;
  const QJsonDocument outputs =
      QJsonDocument::fromJson(outputsJson, &outputParseError);
  if (outputParseError.error != QJsonParseError::NoError ||
      !outputs.isObject()) {
    error = QStringLiteral("Could not parse Niri outputs: %1")
                .arg(outputParseError.errorString());
    return false;
  }
  QJsonObject output = outputs.object().value(outputName).toObject();
  if (output.isEmpty()) {
    error = QStringLiteral("Niri output disappeared during window selection");
    return false;
  }
  if (!output.contains(QStringLiteral("name")))
    output.insert(QStringLiteral("name"), outputName);
  return parseMonitorObject(output, workspaceId, monitor, error);
}

void drawAnnotation(QPainter &painter, const Annotation &annotation) {
  const qreal width = std::max<qreal>(2.0, annotation.size);
  QPen pen(annotation.color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(annotation.color);

  if (annotation.kind == Annotation::Kind::Rectangle) {
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(annotation.start, annotation.end).normalized());
    return;
  }

  if (annotation.kind == Annotation::Kind::Line) {
    painter.drawLine(annotation.start, annotation.end);
    return;
  }

  if (annotation.kind == Annotation::Kind::Freehand ||
      annotation.kind == Annotation::Kind::Highlighter) {
    if (annotation.points.size() < 2)
      return;
    QPainterPath stroke(annotation.points.first());
    for (int index = 1; index + 1 < annotation.points.size(); ++index) {
      const QPointF midpoint =
          (annotation.points.at(index) + annotation.points.at(index + 1)) / 2.0;
      stroke.quadTo(annotation.points.at(index), midpoint);
    }
    stroke.lineTo(annotation.points.last());
    painter.setBrush(Qt::NoBrush);
    if (annotation.kind == Annotation::Kind::Highlighter) {
      QColor ink = annotation.color;
      if (ink.alpha() >= 255)
        ink.setAlpha(120);
      const qreal highlightWidth = std::max<qreal>(6.0, annotation.size * 3.0);
      painter.setPen(QPen(ink, highlightWidth, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
    }
    painter.drawPath(stroke);
    return;
  }

  if (annotation.kind == Annotation::Kind::Arrow) {
    const QLineF line(annotation.start, annotation.end);
    if (line.length() < 1.0)
      return;
    const qreal angle = std::atan2(line.dy(), line.dx());
    const qreal headLength = std::max<qreal>(14.0, annotation.size * 4.2);
    const qreal halfWidth = headLength * 0.46;
    const QPointF direction(std::cos(angle), std::sin(angle));
    const QPointF perpendicular(-direction.y(), direction.x());
    const QPointF base = annotation.end - direction * headLength;
    const QPointF stemEnd = annotation.end - direction * (headLength * 0.5);
    painter.drawLine(annotation.start, stemEnd);
    QPolygonF head;
    head << annotation.end << base + perpendicular * halfWidth
         << base - perpendicular * halfWidth;
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(head);
    return;
  }

  if (annotation.kind == Annotation::Kind::Marker) {
    const qreal diameter = std::max<qreal>(24.0, annotation.size * 6.0);
    const QRectF marker(annotation.start.x() - diameter / 2.0,
                        annotation.start.y() - diameter / 2.0, diameter,
                        diameter);
    painter.setPen(
        QPen(Qt::white, std::max<qreal>(1.0, annotation.size * 0.35)));
    painter.setBrush(annotation.color);
    painter.drawEllipse(marker);
    QFont font(QStringLiteral("Noto Sans"));
    font.setBold(true);
    font.setPixelSize(
        static_cast<int>(std::max<qreal>(11.0, annotation.size * 3.2)));
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(marker, Qt::AlignCenter,
                     QString::number(annotation.number));
    return;
  }

  const QFont font = annotationTextFont(annotation.size);
  painter.setFont(font);
  painter.setPen(annotation.color);
  painter.setBrush(Qt::NoBrush);
  painter.drawText(annotation.start, annotation.text);
}

QRect pixelSelection(const CaptureData &capture, const QRectF &selection) {
  const QRectF bounded = selection.normalized().intersected(
      QRectF(QPointF(), capture.preview.size()));
  const qreal scaleX =
      capture.source.width() / static_cast<qreal>(capture.preview.width());
  const qreal scaleY =
      capture.source.height() / static_cast<qreal>(capture.preview.height());
  const int left =
      std::clamp(static_cast<int>(std::floor(bounded.left() * scaleX)), 0,
                 capture.source.width());
  const int top =
      std::clamp(static_cast<int>(std::floor(bounded.top() * scaleY)), 0,
                 capture.source.height());
  const int right =
      std::clamp(static_cast<int>(std::ceil(bounded.right() * scaleX)), left,
                 capture.source.width());
  const int bottom =
      std::clamp(static_cast<int>(std::ceil(bounded.bottom() * scaleY)), top,
                 capture.source.height());
  return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1));
}

} // namespace

void paintAnnotation(QPainter &painter, const Annotation &annotation) {
  drawAnnotation(painter, annotation);
}

void paintCaptureBackground(QPainter &painter, const QRectF &bounds,
                            BackgroundStyle backgroundStyle) {
  if (backgroundStyle == BackgroundStyle::None)
    return;

  struct Blob {
    QPointF center;
    qreal radius;
    QColor color;
  };
  QColor base;
  std::array<Blob, 4> blobs;
  if (backgroundStyle == BackgroundStyle::Aurora) {
    base = QColor(QStringLiteral("#101827"));
    blobs = {Blob{QPointF(bounds.left() + bounds.width() * 0.15,
                          bounds.top() + bounds.height() * 0.18),
                  bounds.width() * 0.75, QColor(QStringLiteral("#2dd4bf"))},
             Blob{bounds.topRight(), bounds.width() * 0.78,
                  QColor(QStringLiteral("#7c3aed"))},
             Blob{QPointF(bounds.center().x(), bounds.bottom()),
                  bounds.width() * 0.72, QColor(QStringLiteral("#2563eb"))},
             Blob{bounds.bottomLeft(), bounds.width() * 0.55,
                  QColor(QStringLiteral("#0f766e"))}};
  } else if (backgroundStyle == BackgroundStyle::Sunset) {
    base = QColor(QStringLiteral("#251328"));
    blobs = {Blob{bounds.topLeft(), bounds.width() * 0.82,
                  QColor(QStringLiteral("#f97316"))},
             Blob{QPointF(bounds.right(), bounds.top() + bounds.height() * 0.2),
                  bounds.width() * 0.7, QColor(QStringLiteral("#ec4899"))},
             Blob{QPointF(bounds.center().x(), bounds.bottom()),
                  bounds.width() * 0.75, QColor(QStringLiteral("#7c3aed"))},
             Blob{bounds.bottomLeft(), bounds.width() * 0.5,
                  QColor(QStringLiteral("#ef4444"))}};
  } else if (backgroundStyle == BackgroundStyle::Lagoon) {
    base = QColor(QStringLiteral("#071c2a"));
    blobs = {
        Blob{QPointF(bounds.left() + bounds.width() * 0.12, bounds.top()),
             bounds.width() * 0.68, QColor(QStringLiteral("#06b6d4"))},
        Blob{QPointF(bounds.right(), bounds.top() + bounds.height() * 0.25),
             bounds.width() * 0.75, QColor(QStringLiteral("#1d4ed8"))},
        Blob{QPointF(bounds.center().x(), bounds.bottom()),
             bounds.width() * 0.75, QColor(QStringLiteral("#0f766e"))},
        Blob{bounds.bottomLeft(), bounds.width() * 0.5,
             QColor(QStringLiteral("#22d3ee"))}};
  } else {
    base = QColor(QStringLiteral("#171225"));
    blobs = {
        Blob{QPointF(bounds.left(), bounds.top() + bounds.height() * 0.15),
             bounds.width() * 0.7, QColor(QStringLiteral("#a855f7"))},
        Blob{bounds.topRight(), bounds.width() * 0.72,
             QColor(QStringLiteral("#4f46e5"))},
        Blob{bounds.bottomRight(), bounds.width() * 0.65,
             QColor(QStringLiteral("#db2777"))},
        Blob{QPointF(bounds.left() + bounds.width() * 0.25, bounds.bottom()),
             bounds.width() * 0.62, QColor(QStringLiteral("#4338ca"))}};
  }

  painter.fillRect(bounds, base);
  for (const Blob &blob : blobs) {
    QRadialGradient gradient(blob.center, blob.radius);
    QColor center = blob.color;
    center.setAlpha(220);
    QColor edge = blob.color;
    edge.setAlpha(0);
    gradient.setColorAt(0, center);
    gradient.setColorAt(1, edge);
    painter.fillRect(bounds, gradient);
  }
}

bool captureFocusedMonitor(CaptureData &capture, QString &error) {
  const ProcessResult output = runProcess(
      QStringLiteral("niri"), {QStringLiteral("msg"), QStringLiteral("--json"),
                               QStringLiteral("focused-output")});
  if (!output.finished || output.exitCode != 0) {
    error = QStringLiteral("Could not query the focused Niri output: %1")
                .arg(processFailure(output));
    return false;
  }
  const ProcessResult workspaces = runProcess(
      QStringLiteral("niri"), {QStringLiteral("msg"), QStringLiteral("--json"),
                               QStringLiteral("workspaces")});
  if (!workspaces.finished || workspaces.exitCode != 0) {
    error = QStringLiteral("Could not query Niri workspaces: %1")
                .arg(processFailure(workspaces));
    return false;
  }
  if (!parseFocusedMonitor(output.output, workspaces.output, capture.monitor,
                           error))
    return false;

  const QString sourceTemplate =
      runtimePath(QStringLiteral("omasnap-capture-XXXXXX.ppm"));
  if (sourceTemplate.isEmpty()) {
    error = QStringLiteral("Could not create private runtime directory");
    return false;
  }
  QTemporaryFile sourceFile(sourceTemplate);
  sourceFile.setAutoRemove(true);
  if (!sourceFile.open()) {
    error = sourceFile.errorString();
    return false;
  }
  const QString sourcePath = sourceFile.fileName();
  sourceFile.close();

  const QRect geometry = capture.monitor.geometry;
  const ProcessResult grim = runProcess(
      QStringLiteral("grim"),
      {QStringLiteral("-t"), QStringLiteral("ppm"), QStringLiteral("-s"),
       QString::number(capture.monitor.scale, 'g', 8), QStringLiteral("-o"),
       capture.monitor.name, sourcePath},
      {}, 10000);

  if (!grim.finished || grim.exitCode != 0 ||
      !capture.source.load(sourcePath)) {
    error = QStringLiteral("Screen capture failed: %1")
                .arg(QString::fromUtf8(grim.error).trimmed());
    return false;
  }

  capture.preview = capture.source.scaled(
      geometry.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (capture.preview.isNull()) {
    error = QStringLiteral("Could not prepare screenshot preview");
    return false;
  }
  return true;
}

bool captureSelectedWindow(CaptureData &capture, QString &error) {
  const ProcessResult picked =
      runProcess(QStringLiteral("niri"),
                 {QStringLiteral("msg"), QStringLiteral("--json"),
                  QStringLiteral("pick-window")},
                 {}, 300000);
  if (!picked.finished || picked.exitCode != 0) {
    error = QStringLiteral("Niri window selection failed: %1")
                .arg(processFailure(picked));
    return false;
  }

  QString windowId;
  qint64 workspaceId = -1;
  if (!parsePickedWindow(picked.output, windowId, workspaceId, error))
    return false;

  const ProcessResult outputs = runProcess(
      QStringLiteral("niri"), {QStringLiteral("msg"), QStringLiteral("--json"),
                               QStringLiteral("outputs")});
  const ProcessResult workspaces = runProcess(
      QStringLiteral("niri"), {QStringLiteral("msg"), QStringLiteral("--json"),
                               QStringLiteral("workspaces")});
  if (!outputs.finished || outputs.exitCode != 0) {
    error = QStringLiteral("Could not query Niri outputs: %1")
                .arg(processFailure(outputs));
    return false;
  }
  if (!workspaces.finished || workspaces.exitCode != 0) {
    error = QStringLiteral("Could not query Niri workspaces: %1")
                .arg(processFailure(workspaces));
    return false;
  }

  MonitorInfo monitor;
  if (!parseMonitorForWorkspace(outputs.output, workspaces.output, workspaceId,
                                monitor, error))
    return false;

  const QString targetTemplate =
      runtimePath(QStringLiteral("omasnap-window-XXXXXX.png"));
  if (targetTemplate.isEmpty()) {
    error = QStringLiteral("Could not create private runtime directory");
    return false;
  }
  QTemporaryFile targetFile(targetTemplate);
  targetFile.setAutoRemove(true);
  if (!targetFile.open()) {
    error = targetFile.errorString();
    return false;
  }
  const QString targetPath = targetFile.fileName();
  targetFile.close();
  if (!QFile::remove(targetPath)) {
    error = QStringLiteral("Could not prepare native window capture path");
    return false;
  }

  const ClipboardSnapshot clipboard = snapshotClipboard();
  const ProcessResult screenshot =
      runProcess(QStringLiteral("niri"),
                 {QStringLiteral("msg"), QStringLiteral("action"),
                  QStringLiteral("screenshot-window"), QStringLiteral("--id"),
                  windowId, QStringLiteral("--path"), targetPath},
                 {}, 10000);
  if (!screenshot.finished || screenshot.exitCode != 0) {
    QString restoreError;
    if (!restoreClipboard(clipboard, restoreError))
      qWarning().noquote() << restoreError;
    error = QStringLiteral("Niri window capture failed: %1")
                .arg(processFailure(screenshot));
    return false;
  }

  QImage image;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 5000) {
    if (image.load(targetPath))
      break;
    QThread::msleep(20);
  }

  QFile pngFile(targetPath);
  QByteArray png;
  if (pngFile.open(QIODevice::ReadOnly))
    png = pngFile.readAll();
  if (clipboard.valid)
    waitForClipboardImage(png);
  QString restoreError;
  if (!restoreClipboard(clipboard, restoreError))
    qWarning().noquote() << restoreError;

  if (image.isNull()) {
    error = QStringLiteral("Timed out waiting for Niri window capture");
    return false;
  }

  const QSize previewSize{
      std::max(1, qRound(image.width() / monitor.scale)),
      std::max(1, qRound(image.height() / monitor.scale)),
  };
  capture.monitor = std::move(monitor);
  capture.source = std::move(image);
  capture.preview = capture.source.scaled(previewSize, Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
  if (capture.preview.isNull()) {
    error = QStringLiteral("Could not prepare native window preview");
    return false;
  }
  return true;
}

QImage renderCapture(const CaptureData &capture, const QRectF &selection,
                     const QVector<Annotation> &annotations,
                     BackgroundStyle backgroundStyle) {
  const QRect pixels = pixelSelection(capture, selection);
  if (pixels.isEmpty())
    return {};

  QImage cropped = capture.source.copy(pixels).convertToFormat(
      QImage::Format_ARGB32_Premultiplied);
  const qreal sourceScaleX =
      capture.source.width() / static_cast<qreal>(capture.preview.width());
  const qreal sourceScaleY =
      capture.source.height() / static_cast<qreal>(capture.preview.height());
  const bool highDpi = capture.monitor.scale > 1.0;
  const qreal scaleX = highDpi ? capture.monitor.scale : sourceScaleX;
  const qreal scaleY = highDpi ? capture.monitor.scale : sourceScaleY;
  if (highDpi) {
    const QSize impliedSize(std::max(1, qRound(selection.width() * scaleX)),
                            std::max(1, qRound(selection.height() * scaleY)));
    if (cropped.size() != impliedSize) {
      cropped = cropped.scaled(impliedSize, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
    }
  }
  const bool hasBackground = backgroundStyle != BackgroundStyle::None;
  const int marginX =
      hasBackground ? static_cast<int>(std::round(64.0 * scaleX)) : 0;
  const int marginY =
      hasBackground ? static_cast<int>(std::round(64.0 * scaleY)) : 0;
  QImage output(cropped.width() + marginX * 2, cropped.height() + marginY * 2,
                QImage::Format_ARGB32_Premultiplied);
  output.fill(Qt::transparent);

  QPainter painter(&output);
  painter.setRenderHints(QPainter::Antialiasing |
                         QPainter::SmoothPixmapTransform |
                         QPainter::TextAntialiasing);
  if (hasBackground) {
    paintCaptureBackground(painter, output.rect(), backgroundStyle);
    const QRectF imageRect(marginX, marginY, cropped.width(), cropped.height());
    painter.setPen(Qt::NoPen);
    for (int layer = 24; layer > 0; --layer) {
      const qreal spread = layer * std::max(scaleX, scaleY) * 0.85;
      painter.setBrush(QColor(0, 0, 0, 2 + (24 - layer) / 5));
      painter.drawRoundedRect(imageRect.adjusted(-spread, -spread + 14 * scaleY,
                                                 spread, spread + 14 * scaleY),
                              16 * scaleX + spread, 16 * scaleY + spread);
    }
    for (int layer = 12; layer > 0; --layer) {
      const qreal spread = layer * std::max(scaleX, scaleY) * 0.45;
      painter.setBrush(QColor(0, 0, 0, 3 + (12 - layer)));
      painter.drawRoundedRect(imageRect.adjusted(-spread, -spread + 8 * scaleY,
                                                 spread, spread + 8 * scaleY),
                              14 * scaleX + spread, 14 * scaleY + spread);
    }
    QPainterPath clip;
    clip.addRoundedRect(imageRect, 14 * scaleX, 14 * scaleY);
    painter.save();
    painter.setClipPath(clip);
    painter.drawImage(imageRect.topLeft(), cropped);
    painter.restore();
  } else {
    painter.drawImage(QPoint(0, 0), cropped);
  }

  painter.save();
  painter.translate(marginX, marginY);
  painter.scale(scaleX, scaleY);
  painter.setClipRect(QRectF(QPointF(), selection.size()));
  for (const Annotation &annotation : annotations)
    drawAnnotation(painter, annotation);
  painter.restore();
  painter.end();
  return output;
}

bool copyPngFileToClipboard(const QString &path, QString &error) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("Could not read screenshot snapshot: %1").arg(path);
    return false;
  }
  const QByteArray png = input.readAll();
  if (png.isEmpty()) {
    error = QStringLiteral("Screenshot snapshot is empty: %1").arg(path);
    return false;
  }
  return copyToWaylandClipboard(QStringLiteral("image/png"), png, error);
}

QString moveSnapshotToScreenshots(const QString &sourcePath, QString &error) {
  const QString targetPath = screenshotTargetPath(error);
  if (targetPath.isEmpty())
    return {};
  if (QFile::rename(sourcePath, targetPath))
    return targetPath;
  if (QFile::copy(sourcePath, targetPath)) {
    QFile::remove(sourcePath);
    return targetPath;
  }
  error = QStringLiteral("Could not move screenshot snapshot to: %1")
              .arg(targetPath);
  return {};
}

QString temporarySnapshotPath() {
  static const quint32 nonce = QRandomGenerator::global()->generate();
  return runtimePath(QStringLiteral("snapshot-%1-%2.png")
                         .arg(QCoreApplication::applicationPid())
                         .arg(nonce, 8, 16, QChar('0')));
}

bool saveTemporarySnapshot(const QImage &image, QString path, QString &error) {
  if (image.isNull()) {
    error = QStringLiteral("Temporary snapshot is empty");
    return false;
  }
  const QString runtime = secureRuntimeDirectory();
  if (runtime.isEmpty()) {
    error = QStringLiteral("Could not create private runtime directory");
    return false;
  }
  if (path.isEmpty())
    path = temporarySnapshotPath();
  path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  if (QFileInfo(path).absolutePath() != runtime) {
    error =
        QStringLiteral("Temporary snapshots must stay inside %1").arg(runtime);
    return false;
  }

  const QByteArray encodedPath = QFile::encodeName(path);
  const int fd = ::open(encodedPath.constData(),
                        O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
  if (fd < 0 || ::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
    if (fd >= 0)
      ::close(fd);
    error = QStringLiteral("Could not open secure snapshot file: %1").arg(path);
    return false;
  }

  QFile file;
  if (!file.open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
    ::close(fd);
    error = QStringLiteral("Could not open snapshot file handle: %1").arg(path);
    return false;
  }

  if (!image.save(&file, "PNG")) {
    error = QStringLiteral("Could not save temporary snapshot: %1").arg(path);
    return false;
  }
  return true;
}

bool copyTextToClipboard(const QString &text, QString &error) {
  if (text.isEmpty()) {
    error = QStringLiteral("No text found in selection");
    return false;
  }
  return copyToWaylandClipboard(QStringLiteral("text/plain;charset=utf-8"),
                                text.toUtf8(), error);
}

QString recognizeText(const QImage &image, QString &error) {
  const QString inputTemplate =
      runtimePath(QStringLiteral("omasnap-ocr-XXXXXX.png"));
  if (inputTemplate.isEmpty()) {
    error = QStringLiteral("Could not create private runtime directory");
    return {};
  }
  QTemporaryFile input(inputTemplate);
  input.setAutoRemove(true);
  if (!input.open()) {
    error = input.errorString();
    return {};
  }
  const QString path = input.fileName();
  input.close();
  if (!image.save(path, "PNG")) {
    error = QStringLiteral("Could not prepare image for OCR");
    return {};
  }

  const QString languages =
      qEnvironmentVariable("OMASNAP_OCR_LANGS", QStringLiteral("eng"));
  const ProcessResult result = runProcess(
      QStringLiteral("tesseract"),
      {path, QStringLiteral("stdout"), QStringLiteral("--oem"),
       QStringLiteral("1"), QStringLiteral("--psm"), QStringLiteral("6"),
       QStringLiteral("-l"), languages, QStringLiteral("--dpi"),
       QStringLiteral("300"), QStringLiteral("-c"),
       QStringLiteral("preserve_interword_spaces=1")},
      {}, 30000);
  if (!result.finished || result.exitCode != 0) {
    error = QStringLiteral("OCR failed: %1")
                .arg(QString::fromUtf8(result.error).trimmed());
    return {};
  }
  const QString text = QString::fromUtf8(result.output).trimmed();
  if (text.isEmpty())
    error = QStringLiteral("No text found in selection");
  return text;
}

void sendCaptureNotification(const QString &message, const QString &imagePath) {
  if (qEnvironmentVariableIsSet("OMASNAP_DISABLE_NOTIFICATIONS"))
    return;
  if (!imagePath.isEmpty()) {
    if (!QProcess::startDetached(
            QCoreApplication::applicationFilePath(),
            {QStringLiteral("--saved-notification"), imagePath})) {
      qWarning() << "Could not start saved screenshot notification helper";
    }
    return;
  }
  QStringList arguments{
      QStringLiteral("--app-name"),    QStringLiteral("omasnap"),
      QStringLiteral("--expire-time"), QStringLiteral("4500"),
      QStringLiteral("--icon"),        QStringLiteral("camera-photo"),
      QStringLiteral("Omasnap"),       message};
  QProcess::startDetached(QStringLiteral("notify-send"), arguments);
}
