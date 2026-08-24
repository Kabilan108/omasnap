/** @fileoverview Declares screenshot capture, rendering, and output types. */
#pragma once

#include "cut.hpp"

#include <cstdint>

#include <QPainterPath>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

class QFont;
class QPainter;
class QProcess;
class QByteArray;

struct MonitorInfo {
  QString name;
  QRect geometry;
  QSize pixelSize;
  qreal scale = 1.0;
  qint64 workspaceId = -1;
};

struct CaptureData {
  MonitorInfo monitor;
  QImage source;
  /** Logical size the native source image is presented at. */
  QSize previewSize;
};

enum class BackgroundStyle { None, Aurora, Sunset, Lagoon, Violet };
enum class QuickOutputMode { None, Copy, Save, Both };

enum class SpotlightShape { Ellipse, Rectangle, RoundedRectangle };
enum class RedactionStyle { Solid, Pixelate };
enum class TextBackground { Plain, Pill };

struct Annotation {
  enum class Kind {
    Arrow,
    Line,
    Freehand,
    Highlighter,
    Marker,
    Rectangle,
    Ellipse,
    Text,
    Redaction,
    Spotlight
  };

  Kind kind = Kind::Arrow;
  QPointF start;
  QPointF end;
  QString text;
  QColor color;
  qreal size = 4.0;
  int number = 0;
  QVector<QPointF> points;
  bool filled = false;
  qreal cornerRadius = 0.0;
  RedactionStyle redactionStyle = RedactionStyle::Pixelate;
  qreal magnification = 2.0;
  SpotlightShape spotlightShape = SpotlightShape::Ellipse;
  quint32 redactionSeed = 0;
  TextBackground textBackground = TextBackground::Pill;
  quint64 id = 0;

  bool operator==(const Annotation &) const = default;
};

struct Operation {
  enum class Type { Crop, Background, Annotate, Patch, Delete, Cut };

  Type type = Type::Annotate;
  QRectF crop;
  BackgroundStyle background = BackgroundStyle::None;
  QVector<Annotation> annotations;
  QVector<quint64> ids;
  CutOp cut;

  bool operator==(const Operation &) const = default;
};

struct OperationLog {
  QVector<Operation> ops;
  int index = 0;
  quint64 nextId = 1;
  int nextMarker = 1;

  bool operator==(const OperationLog &) const = default;
};

enum class AnnotationLayer { Redaction, Default };

[[nodiscard]] constexpr AnnotationLayer annotationLayer(Annotation::Kind kind) {
  return kind == Annotation::Kind::Redaction ? AnnotationLayer::Redaction
                                             : AnnotationLayer::Default;
}

[[nodiscard]] bool loadCaptureFonts();
[[nodiscard]] QFont annotationTextFont(qreal size);
/**
 * Discovers the focused Niri output and its active workspace.
 */
[[nodiscard]] bool probeFocusedMonitor(MonitorInfo &monitor, QString &error);
/**
 * Captures the focused output with grim.
 */
[[nodiscard]] bool captureMonitorPixels(const MonitorInfo &monitor,
                                        CaptureData &capture, QString &error);
/** Convenience: probes the focused monitor, then captures its pixels. */
[[nodiscard]] bool captureFocusedMonitor(CaptureData &capture, QString &error);
/** Queries the regular window focused before an exclusive layer is mapped. */
[[nodiscard]] bool queryFocusedWindow(QByteArray &windowJson, QString &error);
/** Uses Niri's focused-window query and screenshot action for a clean capture. */
[[nodiscard]] bool captureFocusedWindow(CaptureData &capture, QString &error);
/** Completes clean capture from Niri window JSON containing id and workspace_id. */
[[nodiscard]] bool capturePickedWindow(const QByteArray &pickedJson,
                                       CaptureData &capture, QString &error);
/** Bounds of a text layer's glyph box, or of its readability pill when it
 *  has one; `start` is the baseline origin. */
[[nodiscard]] QRectF annotationTextBounds(const Annotation &annotation);
[[nodiscard]] QString operationLogPath(const QString &imagePath);
[[nodiscard]] bool saveOperationLog(const QString &path, const OperationLog &log,
                                    QString &error);
[[nodiscard]] bool loadOperationLog(const QString &path, OperationLog &log,
                                    QString &error);
[[nodiscard]] QString temporaryExportPath();
[[nodiscard]] QImage renderCapture(const CaptureData &capture,
                                   const QRectF &selection,
                                   const QVector<Annotation> &annotations,
                                   BackgroundStyle backgroundStyle);
/** Loads the current Wayland clipboard image. */
[[nodiscard]] bool loadClipboardImage(QImage &image, QString &error);
[[nodiscard]] bool copyPngFileToClipboard(const QString &path, QString &error);
[[nodiscard]] bool copyImageToClipboard(const QImage &image, QString &error);
[[nodiscard]] bool quickOutput(const QImage &image, QuickOutputMode mode,
                               QString &error);
[[nodiscard]] bool copyTextToClipboard(const QString &text, QString &error);
void paintAnnotation(QPainter &painter, const Annotation &annotation);
[[nodiscard]] QPainterPath spotlightPath(const Annotation &annotation);
void paintSpotlights(QPainter &painter, const QImage &source,
                     const QRectF &targetBounds, const QRectF &sourceRect,
                     const QVector<Annotation> &annotations);
/**
 * Paints the default annotation layer (spotlights, then vectors) in selection
 * space. Spotlights sample `redacted`, which must already include the
 * redaction layer so a loupe cannot magnify source pixels.
 */
void paintDefaultLayer(QPainter &painter, const QImage &redacted,
                       const QRectF &logicalBounds,
                       const QVector<Annotation> &annotations);
void paintCaptureBackground(QPainter &painter, const QRectF &bounds,
                            BackgroundStyle backgroundStyle);
/**
 * Renders the selection region at `targetSize` for the redaction layer. The
 * result carries no annotations; callers overlay redactions with
 * applyRedactionsScaled and cache it while the selection is unchanged.
 */
[[nodiscard]] QImage renderSelectionBase(const CaptureData &capture,
                                         const QRectF &selection,
                                         const QSize &targetSize);
/**
 * Paints redaction annotations over a display-resolution selection image. The
 * source image MUST be the exact selection region scaled to `targetSize`;
 * annotations are selection-relative, spanning 0..`selection` size.
 */
QImage applyRedactionsScaled(QImage image, const QVector<Annotation> &redactions,
                             const QRectF &selection, const QSizeF &targetSize);
/** Creates or repairs a private directory owned by the current user. */
[[nodiscard]] bool ensurePrivateDirectory(const QString &path);
/** Returns Omasnap's private runtime directory, or empty on failure. */
[[nodiscard]] QString secureRuntimeDirectory();
[[nodiscard]] QString moveSnapshotToScreenshots(const QString &sourcePath,
                                                QString &error);
[[nodiscard]] QString temporarySnapshotPath();
[[nodiscard]] QString pinnedSnapshotPath(int index);
void prunePinnedSnapshots();
/**
 * Writes `image` into the private runtime directory. `quality` is the Qt PNG
 * quality knob, which maps inversely onto zlib levels: -1 keeps the default
 * level, higher values compress less and encode faster.
 */
[[nodiscard]] bool saveTemporarySnapshot(const QImage &image, QString path,
                                         QString &error, int quality = -1);
[[nodiscard]] QString recognizeText(const QImage &image, QString &error);
void sendCaptureNotification(const QString &message,
                             const QString &imagePath = {});
