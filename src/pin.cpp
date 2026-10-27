#include "pin.hpp"
#include "capture.hpp"
#include "chrome-theme.hpp"
#include "icons.hpp"
#include "overlay-chrome.hpp"
#include "pin-expiry.hpp"
#include "pin-file.hpp"
#include "pin-layout.hpp"
#include "startup-timing.hpp"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QDrag>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QMargins>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace {

constexpr int kCornerMargin = 14;
constexpr int kPinGap = 10;
constexpr int kToastMs = 1200;
/// Enters this soon after mapping come from a pointer already resting there.
constexpr int kMapEnterMs = 300;

QString runForOutput(const QString &program, const QStringList &arguments,
                     bool *ok = nullptr, int timeoutMs = 500) {
  if (ok)
    *ok = false;
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    process.waitForFinished(500);
    return {};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    return {};
  if (ok)
    *ok = true;
  return QString::fromUtf8(process.readAllStandardOutput());
}

QString sharedPinPath(const QString &path, const QString &shared,
                      QString &error) {
  if (!shared.isEmpty() && QFileInfo::exists(shared))
    return shared;
  OperationLog metadata;
  const QString sidecar = operationLogPath(path);
  if (QFile::exists(sidecar)) {
    if (!loadOperationLog(sidecar, metadata, error))
      return {};
    if (!metadata.savedPath.isEmpty()) {
      if (QFileInfo(metadata.savedPath).isFile())
        return metadata.savedPath;
      error = QStringLiteral("Saved screenshot is no longer available");
      return {};
    }
  }
  const QFileInfo source(path);
  if (!source.isFile()) {
    error = QStringLiteral("Screenshot file is no longer available");
    return {};
  }
  // Runtime previews disappear when closed. Path sharing and revealing use
  // one durable copy of the current rendered PNG, including returned edits.
  return source.absolutePath() == secureRuntimeDirectory()
             ? copySnapshotToScreenshots(path, error) : source.absoluteFilePath();
}

/// A program's name, seen through Nix's ".name-wrapped" executables.
QString programName(const QString &executable) {
  QString name = QFileInfo(executable).fileName();
  while (name.startsWith(QLatin1Char('.')) &&
         name.endsWith(QLatin1StringView("-wrapped")))
    name = name.mid(1).chopped(8);
  return name;
}

/// The program an Exec key in a desktop or D-Bus service file launches.
QString execProgramName(const QString &path, QLatin1StringView group) {
  QFile file(path);
  if (path.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
    return {};
  const QString header = QStringLiteral("[%1]").arg(group);
  bool inGroup = false;
  while (!file.atEnd()) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    if (line.startsWith(QLatin1Char('[')))
      inGroup = line == header;
    else if (inGroup && line.startsWith(QLatin1StringView("Exec="))) {
      QStringList command = QProcess::splitCommand(line.mid(5));
      if (!command.isEmpty() && programName(command.constFirst()) == QLatin1StringView("env")) {
        command.removeFirst();
        while (!command.isEmpty() && command.constFirst().contains(QLatin1Char('=')))
          command.removeFirst();
      }
      return programName(command.value(0));
    }
  }
  return {};
}

/// Whether the generic FileManager1 name would reach `program`: the bus
/// routes to whichever process owns it now, or else starts its service file.
bool genericFileManagerRuns(const QString &program, const QString &service) {
  if (program.isEmpty() ||
      program != execProgramName(
                     QStandardPaths::locate(
                         QStandardPaths::GenericDataLocation,
                         QStringLiteral("dbus-1/services/%1.service").arg(service)),
                     QLatin1StringView("D-BUS Service")))
    return false;
  bool owned = false;
  const QString status = runForOutput(
      QStringLiteral("busctl"),
      {QStringLiteral("--user"), QStringLiteral("--timeout=1s"),
       QStringLiteral("status"), service},
      &owned, 2000);
  if (!owned)
    return true;
  for (const QString &line : status.split(QLatin1Char('\n'))) {
    if (line.startsWith(QLatin1StringView("Exe=")))
      return programName(line.mid(4).trimmed()) == program;
  }
  return false;
}

bool revealFileInFolder(const QString &path, QString &error) {
  const QFileInfo file(path);
  if (!file.isFile()) {
    error = QStringLiteral("Screenshot file is no longer available");
    return false;
  }
  // The generic FileManager1 service can belong to a different application
  // than the user's folder default. Ask the configured application itself:
  // D-Bus-activatable desktop IDs name their service without ".desktop", and
  // other file managers are reached through FileManager1 only when that
  // service launches the same program.
  const QString desktopId = runForOutput(
      QStringLiteral("xdg-mime"), {QStringLiteral("query"),
                                    QStringLiteral("default"),
                                    QStringLiteral("inode/directory")}).trimmed();
  if (desktopId.endsWith(QLatin1StringView(".desktop"))) {
    QStringList services;
    const QString application = desktopId.chopped(8);
    if (application.contains(QLatin1Char('.')))
      services << application;
    const QString program = execProgramName(
        QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId),
        QLatin1StringView("Desktop Entry"));
    const QString genericService = QStringLiteral("org.freedesktop.FileManager1");
    if (genericFileManagerRuns(program, genericService))
      services << genericService;
    for (const QString &service : std::as_const(services)) {
      bool revealed = false;
      static_cast<void>(runForOutput(
          QStringLiteral("busctl"),
          {QStringLiteral("--user"), QStringLiteral("--timeout=3s"),
           QStringLiteral("--"), QStringLiteral("call"), service,
           QStringLiteral("/org/freedesktop/FileManager1"),
           QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"),
           QStringLiteral("ass"), QStringLiteral("1"),
           QUrl::fromLocalFile(file.absoluteFilePath()).toString(QUrl::FullyEncoded),
           QString()}, &revealed, 4000));
      if (revealed)
        return true;
    }
  }
  // File managers without ShowItems still open their containing folder via
  // the user's association.
  if (QProcess::startDetached(
          QStringLiteral("xdg-open"),
          {QUrl::fromLocalFile(file.absolutePath()).toString(QUrl::FullyEncoded)}))
    return true;
  error = QStringLiteral("Could not open the file browser");
  return false;
}

struct DragPayload {
  QByteArray png;
  QImage preview;
  QString savedPath;
};

struct PinPreview {
  QImage image;
  QByteArray png;
  QImage drag;
  bool replaced = false;
};

struct ActionResult {
  QString error;
  QString sharedPath;
  std::shared_ptr<PinSnapshotFile> document;
};

class PinWindow final : public QWidget {
  friend bool runPinRevealSmoke(QString &error);
  friend bool runPinInteractionChecks(QString &error);

public:
  PinWindow(QImage image, QString path, const QSize &frame,
            PinLifetime lifetime)
      : image_(std::move(image)), expiry_(lifetime == PinLifetime::Persistent),
        path_(std::move(path)), snapshotFile_(path_) {
    setWindowTitle(QStringLiteral("omasnap-pin"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(frame);
    setMouseTracking(true);
    connect(&expiry_, &PinExpiry::opacityChanged, this, [this](qreal opacity) {
      opacity_ = opacity;
      update();
    });
    connect(&expiry_, &PinExpiry::expired, this, &QWidget::close);
    connect(&documentFiles_, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &changed) {
      // QSaveFile replaces the inode, so each completed edit needs a new watch.
      documentFiles_.addPath(changed);
      reloadPinDocument();
    });
    connect(&documentWatcher_, &QFutureWatcher<PinPreview>::finished, this, [this] {
      documentLoading_ = false;
      const auto preview = documentWatcher_.result();
      if (preview.replaced) {
        close();
        return;
      }
      if (!closing_ && !preview.image.isNull()) {
        image_ = preview.image;
        dragPng_ = preview.png;
        dragPreview_ = preview.drag;
        path_ = pinDocument_->previewPath();
        sharedPath_.clear();
        savedPath_.clear();
        ++previewRevision_;
        update();
      }
      if (std::exchange(documentReloadPending_, false))
        reloadPinDocument();
    });
    auto *payload = new QFutureWatcher<DragPayload>(this);
    connect(payload, &QFutureWatcher<DragPayload>::finished, this,
            [this, payload, path = path_] {
      auto result = payload->result();
      if (path_ == path) {
        dragPng_ = std::move(result.png);
        dragPreview_ = std::move(result.preview);
        savedPath_ = std::move(result.savedPath);
      }
      payload->deleteLater();
    });
    payload->setFuture(QtConcurrent::run([image = image_, path = path_] {
      // A dropped file keeps the name of the screenshot already saved for
      // this capture, not the private runtime preview's.
      QString savedPath;
      OperationLog log;
      QString ignored;
      const QString sidecar = operationLogPath(path);
      if (QFile::exists(sidecar) && loadOperationLog(sidecar, log, ignored) &&
          QFileInfo(log.savedPath).isFile())
        savedPath = log.savedPath;
      // Public --pin files need not be PNGs; offer PNG bytes either way.
      QByteArray png;
      if (QImageReader::imageFormat(path) == "png") {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
          png = file.readAll();
      } else {
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
          png.clear();
      }
      return DragPayload{png,
                         image.scaled(256, 256, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation),
                         savedPath};
    }));
  }

  [[nodiscard]] bool hasPinLock() const {
    return snapshotFile_.isLocked() && slotLock_.isLocked();
  }

  [[nodiscard]] int slotIndex() const { return slotLock_.index(); }

  [[nodiscard]] QSize availableSize() const {
    const QScreen *target =
        screen() ? screen() : QGuiApplication::primaryScreen();
    return target ? target->availableGeometry().size() : QSize(1920, 1080);
  }

  /// Places the pin in its stacking slot once its screen and size are known.
  void placeInSlot() {
    applyPosition(pinSlotPosition(availableSize(), size(), size(), slotIndex(),
                                  kPinGap, kCornerMargin));
  }

protected:
  void showEvent(QShowEvent *event) override {
    QWidget::showEvent(event);
    shownClock_.start();
    expiry_.start();
  }

  void paintEvent(QPaintEvent *) override {
    if (!painted_) {
      startupTimingMark("pin first paint");
      painted_ = true;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setOpacity(opacity_);

    QPainterPath card;
    card.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 7, 7);
    painter.save();
    painter.setClipPath(card);
    painter.fillRect(rect(), chromeTheme().surface);
    if (!image_.isNull()) {
      // Cover the frame, centered horizontally and aligned at the top.
      const qreal scale =
          std::max(static_cast<qreal>(width()) / image_.width(),
                   static_cast<qreal>(height()) / image_.height());
      const QRectF source((image_.width() - width() / scale) / 2.0, 0.0,
                          width() / scale, height() / scale);
      painter.drawImage(QRectF(rect()), image_, source);
    }
    painter.restore();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen((hovered_ ? chromeTheme().activeBorder
                                  : chromeTheme().inactiveBorder).brush(rect()), 1.5));
    painter.drawPath(card);
    if (expiry_.kept() && !hovered_)
      drawControlButton(painter, pinButtonRect(), QStringLiteral("pin"));
    if (!toast_.isEmpty())
      paintToast(painter);
    if (!hovered_)
      return;

    drawControlButton(painter, dragButtonRect(), QStringLiteral("drag-handle"));
    drawControlButton(painter, editButtonRect(), QStringLiteral("edit"),
                      QStringLiteral("Edit"));
    drawControlButton(painter, pinButtonRect(), QStringLiteral("pin"));
    drawControlButton(painter, pathButtonRect(), QStringLiteral("path"));
    drawControlButton(painter, revealButtonRect(), QStringLiteral("folder"));
    drawControlButton(painter, copyButtonRect(), QStringLiteral("copy"),
                      QStringLiteral("Copy"));
    drawControlButton(painter, closeButtonRect(), QStringLiteral("close"));
    paintHoverTip(painter);
  }

  void drawControlButton(QPainter &painter, const QRectF &rect,
                         const QString &action,
                         const QString &label = {}) const {
    painter.setPen(Qt::NoPen);
    const bool hovered = hovered_ && rect == controlRect(hoveredControl_);
    painter.setBrush(hovered ? chromeTheme().buttonHover : chromeTheme().button);
    painter.drawRoundedRect(rect, 6, 6);
    // Kept state is independent of hover. Gradient borders use their leading
    // color for the small pin glyph.
    const QColor foreground = action == QStringLiteral("pin") && expiry_.kept()
                                  ? chromeTheme().activeBorder.colors.constFirst()
                              : hovered ? chromeTheme().buttonHoverText
                                        : chromeTheme().buttonText;
    if (label.isEmpty()) {
      drawToolbarIcon(painter, rect, action, {}, foreground);
    } else {
      painter.setFont(chromeFont(11, true));
      painter.setPen(foreground);
      painter.drawText(rect, Qt::AlignCenter, label);
    }
  }

  // Layer surfaces cannot reliably map an independent QToolTip toplevel, so
  // the hint is drawn inside the pin like the toast pill.
  void paintHoverTip(QPainter &painter) const {
    const QString tip = pinControlTip(hoveredControl_, expiry_.kept());
    if (tip.isEmpty())
      return;
    painter.setFont(chromeFont(11));
    const QFontMetrics metrics(painter.font());
    const QRectF control = controlRect(hoveredControl_);
    const qreal tipWidth =
        std::min<qreal>(width() - 8, metrics.horizontalAdvance(tip) + 16);
    const qreal x = std::clamp(control.center().x() - tipWidth / 2, 4.0,
                               width() - tipWidth - 4.0);
    const QRectF pill(x, control.bottom() + 4, tipWidth, 20);
    painter.setPen(QPen(chromeTheme().tooltipBorder, 1));
    painter.setBrush(chromeTheme().tooltipBackground);
    painter.drawRoundedRect(pill, 4, 4);
    painter.setPen(chromeTheme().tooltipText);
    painter.drawText(pill, Qt::AlignCenter,
                     metrics.elidedText(tip, Qt::ElideRight,
                                        static_cast<int>(tipWidth - 8)));
  }

  void paintToast(QPainter &painter) const {
    painter.setFont(chromeFont(11));
    const QFontMetrics metrics(painter.font());
    const qreal toastWidth =
        std::min<qreal>(width() - 8, metrics.horizontalAdvance(toast_) + 28);
    const QRectF pill((width() - toastWidth) / 2.0, height() - 32, toastWidth,
                      24);
    painter.setPen(Qt::NoPen);
    painter.setBrush(chromeTheme().toastBackground);
    painter.drawRoundedRect(pill, 12, 12);
    painter.setPen(chromeTheme().toastText);
    painter.drawText(pill, Qt::AlignCenter,
                     metrics.elidedText(toast_, Qt::ElideRight,
                                        static_cast<int>(toastWidth - 16)));
  }

  void mousePressEvent(QMouseEvent *event) override {
    lastPointer_ = event->position();
    pendingKeyboard_ = false;
    setKeyboardFocus(true);
    if (event->button() == Qt::MiddleButton) {
      close();
      return;
    }
    if (event->button() != Qt::LeftButton)
      return;
    const QPointF position = event->position();
    if (closeButtonRect().contains(position)) {
      close();
      return;
    }
    if (pinButtonRect().contains(position)) {
      toggleKept();
      return;
    }
    if (dragButtonRect().contains(position)) {
      beginFileDrag();
      return;
    }
    if (copyButtonRect().contains(position)) {
      copyImage();
      return;
    }
    if (pathButtonRect().contains(position)) {
      copyPath();
      return;
    }
    if (revealButtonRect().contains(position)) {
      revealFile();
      return;
    }
    if (editButtonRect().contains(position)) {
      reopenInEditor();
      return;
    }
    dragging_ = true;
    dragOffset_ = position.toPoint();
    dragStart_ = position_;
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    const QPointF position = event->position();
    lastPointer_ = position;
    if (pendingKeyboard_ && position != pendingPosition_) {
      pendingKeyboard_ = false;
      setKeyboardFocus(true);
    }
    if (dragging_) {
      setCursor(Qt::ClosedHandCursor);
      event->accept();
      return;
    }
    const int control = controlRectAt(position);
    setCursor(control >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (control != hoveredControl_) {
      hoveredControl_ = control;
      update();
    }
  }

  // Qt cannot know a layer surface's global position on Wayland, and moving
  // the surface mid-drag makes later pointer positions relative to whichever
  // origin the compositor has applied by then. The implicit grab keeps
  // reporting positions against the unmoved surface, so the drop is exact.
  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton && dragging_) {
      dragging_ = false;
      setCursor(Qt::ArrowCursor);
      const QPoint delta = event->position().toPoint() - dragOffset_;
      if (!delta.isNull()) {
        applyPosition(clampPinGeometry(QRect(dragStart_ + delta, size()),
                                       QRect(QPoint(), availableSize()))
                          .topLeft());
        // Moving a preview expresses intent to keep it; a click does not.
        expiry_.setKept(true);
      }
    }
    QWidget::mouseReleaseEvent(event);
  }

  void wheelEvent(QWheelEvent *event) override {
    // Pinned captures deliberately keep a stable display-shaped frame so the
    // controls remain usable and the image area never reflows.
    event->accept();
  }

  void keyPressEvent(QKeyEvent *event) override {
    if (event->isAutoRepeat()) {
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_P && event->modifiers() == Qt::ControlModifier) {
      toggleKept();
      return;
    }
    if (event->key() == Qt::Key_X && event->modifiers() == Qt::NoModifier) {
      close();
      return;
    }
    if (hovered_ && event->modifiers() == Qt::NoModifier) {
      switch (event->key()) {
      case Qt::Key_T:
        toggleKept();
        return;
      case Qt::Key_A:
      case Qt::Key_E:
        reopenInEditor();
        return;
      case Qt::Key_C:
        copyImage();
        return;
      case Qt::Key_L:
      case Qt::Key_F:
        copyPath();
        return;
      case Qt::Key_R:
        revealFile();
        return;
      default:
        break;
      }
    }
    if (event->key() == Qt::Key_Escape) {
      close();
      return;
    }
    if (event->matches(QKeySequence::Copy)) {
      copyImage();
      return;
    }
    QWidget::keyPressEvent(event);
  }

  void closeEvent(QCloseEvent *event) override {
    closing_ = true;
    updateExpiryPause();
    QWidget::closeEvent(event);
  }

  void enterEvent(QEnterEvent *event) override {
    hovered_ = true;
    // Niri also sends an enter without pointer motion when the pin maps under
    // a resting pointer, or when a surface covering it (such as the next
    // capture overlay) unmaps. Those wait for motion or a click before taking
    // the keyboard; the pointer moving onto a pin takes it immediately.
    const bool mapping =
        !shownClock_.isValid() || shownClock_.elapsed() < kMapEnterMs;
    const bool unmoved = event->position() == lastPointer_;
    lastPointer_ = event->position();
    if (mapping || unmoved) {
      pendingKeyboard_ = true;
      pendingPosition_ = event->position();
    } else {
      setKeyboardFocus(true);
    }
    updateExpiryPause();
    hoveredControl_ = controlRectAt(event->position());
    setCursor(hoveredControl_ >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
  }

  void leaveEvent(QEvent *) override {
    hovered_ = false;
    setKeyboardFocus(false);
    pendingKeyboard_ = false;
    updateExpiryPause();
    hoveredControl_ = -1;
    setCursor(Qt::ArrowCursor);
    update();
  }

private:
  // Niri focuses an on-demand layer as soon as it maps and never on hover,
  // so a pin maps with no keyboard interactivity and holds the keyboard only
  // while the pointer is over it. Leaving hands focus back to the window.
  // Niri gives the keyboard only to layers on its active output.
  void setKeyboardFocus(bool focused) {
    QWindow *handle = windowHandle();
    LayerShellQt::Window *layer =
        handle ? LayerShellQt::Window::get(handle) : nullptr;
    if (!layer)
      return;
    layer->setKeyboardInteractivity(
        focused ? LayerShellQt::Window::KeyboardInteractivityExclusive
                : LayerShellQt::Window::KeyboardInteractivityNone);
  }

  void runAction(std::function<ActionResult()> worker, QString message) {
    if (actionPending_)
      return;
    actionPending_ = true;
    updateExpiryPause();
    auto *watcher = new QFutureWatcher<ActionResult>(this);
    connect(watcher, &QFutureWatcher<ActionResult>::finished, this,
            [this, watcher, message, revision = previewRevision_] {
      const auto result = watcher->result();
      watcher->deleteLater();
      actionPending_ = false;
      updateExpiryPause();
      // Reuse a successful save even if copying or revealing it failed, but
      // not after a returned edit replaced the image it was saved from.
      if (!result.sharedPath.isEmpty() && revision == previewRevision_)
        sharedPath_ = result.sharedPath;
      if (result.document && !pinDocument_) {
        pinDocument_ = result.document;
        documentFiles_.addPath(operationLogPath(pinDocument_->path()));
        reloadPinDocument();
      }
      if (!result.error.isEmpty())
        showToast(result.error);
      else if (!message.isEmpty())
        showToast(message);
    });
    watcher->setFuture(QtConcurrent::run(std::move(worker)));
  }

  void copyImage() {
    runAction([path = path_, image = image_] {
      QString error;
      // Internal thumbnails are display-only. Public --pin files may use
      // another format, in which case image still holds their full pixels.
      // A missing internal PNG must fail rather than copy its thumbnail.
      if (PinSnapshotFile::isOwnedPath(path) ||
          QImageReader::imageFormat(path) == "png")
        static_cast<void>(copyPngFileToClipboard(path, error));
      else
        static_cast<void>(copyImageToClipboard(image, error));
      return ActionResult{error, {}, {}};
    }, QStringLiteral("Copied to clipboard"));
  }

  void copyPath() {
    runAction([path = path_, shared = sharedPath_] {
      QString error;
      const QString saved = sharedPinPath(path, shared, error);
      if (!saved.isEmpty())
        static_cast<void>(copyTextToClipboard(saved, error));
      return ActionResult{error, saved, {}};
    }, QStringLiteral("Copied path"));
  }

  // The editor or file manager this opens needs the keyboard even while the
  // pointer stays on the pin; fresh motion over the pin takes it back.
  void yieldKeyboard() {
    setKeyboardFocus(false);
    if (hovered_) {
      pendingKeyboard_ = true;
      pendingPosition_ = lastPointer_;
    }
  }

  void revealFile() {
    yieldKeyboard();
    runAction([path = path_, shared = sharedPath_] {
      QString error;
      const QString saved = sharedPinPath(path, shared, error);
      if (!saved.isEmpty())
        static_cast<void>(revealFileInFolder(saved, error));
      return ActionResult{error, saved, {}};
    }, {});
  }

  void reopenInEditor() {
    yieldKeyboard();
    const QString output = screen() ? screen()->name() : QString();
    runAction([program = QCoreApplication::applicationFilePath(), path = path_,
               document = pinDocument_, output]() mutable -> ActionResult {
      QString error;
      if (!document)
        document = copyPinDocument(path, error);
      if (!document)
        return {error, {}, {}};
      QStringList arguments{QStringLiteral("--file"), document->path(),
                            QStringLiteral("--pin-document"), document->path()};
      if (!output.isEmpty())
        arguments << QStringLiteral("--output") << output;
      if (!QProcess::startDetached(program, arguments))
        return {QStringLiteral("Could not start omasnap"), {}, document};
      return {{}, {}, document};
    }, {});
  }

  void reloadPinDocument() {
    if (closing_ || !pinDocument_)
      return;
    if (documentLoading_) {
      documentReloadPending_ = true;
      return;
    }
    documentLoading_ = true;
    documentWatcher_.setFuture(QtConcurrent::run([document = pinDocument_] {
      PinPreview result;
      OperationLog log;
      QString error;
      if (loadOperationLog(operationLogPath(document->path()), log, error) &&
          !log.savedPath.isEmpty()) {
        result.replaced = true;
        return result;
      }
      QFile file(document->previewPath());
      if (file.open(QIODevice::ReadOnly)) {
        result.png = file.readAll();
        result.image = QImage::fromData(result.png, "PNG");
        if (!result.image.isNull())
          result.drag = result.image.scaled(256, 256, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
      }
      return result;
    }));
  }

  // A layer surface can still initiate a Wayland uri-list drag just like a
  // file manager; the six-dot control is the drag handle. Its optional PNG
  // and thumbnail payloads were prepared on a worker.
  void beginFileDrag() {
    fileDragActive_ = true;
    updateExpiryPause();
    QMimeData *mime = new QMimeData;
    const QList<QUrl> urls{QUrl::fromLocalFile(dragFilePath())};
    mime->setUrls(urls);
    mime->setText(urls.constFirst().toLocalFile());
    if (!dragPng_.isEmpty())
      mime->setData(QStringLiteral("image/png"), dragPng_);
    QDrag drag(this);
    drag.setMimeData(mime);
    if (!dragPreview_.isNull())
      drag.setPixmap(QPixmap::fromImage(dragPreview_));
    drag.exec(Qt::CopyAction | Qt::MoveAction);
    fileDragActive_ = false;
    updateExpiryPause();
  }

  [[nodiscard]] QString dragFilePath() const {
    for (const QString &saved : {sharedPath_, savedPath_}) {
      if (!saved.isEmpty() && QFileInfo::exists(saved))
        return saved;
    }
    return path_;
  }

  void updateExpiryPause() {
    expiry_.setPaused(closing_ || hovered_ || actionPending_ || fileDragActive_);
  }

  void toggleKept() {
    expiry_.setKept(!expiry_.kept());
    showToast(expiry_.kept() ? QStringLiteral("Pinned")
                             : QStringLiteral("Fades after 10 seconds"));
  }

  void showToast(QString message) {
    toast_ = std::move(message);
    update();
    QTimer::singleShot(kToastMs, this, [this] {
      toast_.clear();
      update();
    });
  }

  void applyPosition(QPoint position) {
    position_ = position;
    if (QWindow *handle = windowHandle()) {
      if (LayerShellQt::Window *layer = LayerShellQt::Window::get(handle)) {
        const QSize available = availableSize();
        layer->setMargins(
            QMargins(0, 0, available.width() - position.x() - width(),
                     available.height() - position.y() - height()));
        // Margins take effect on the next surface commit. Repaint now, or a
        // dropped pin waits for some later hover change to move.
        update();
      }
    }
  }

  // Painting, hover feedback, and clicks all use the same control geometry.
  [[nodiscard]] QRectF closeButtonRect() const { return controlRect(0); }

  [[nodiscard]] QRectF copyButtonRect() const { return controlRect(1); }

  [[nodiscard]] QRectF pathButtonRect() const { return controlRect(2); }

  [[nodiscard]] QRectF editButtonRect() const { return controlRect(3); }

  [[nodiscard]] QRectF dragButtonRect() const { return controlRect(4); }

  [[nodiscard]] QRectF pinButtonRect() const { return controlRect(5); }

  [[nodiscard]] QRectF revealButtonRect() const { return controlRect(6); }

  [[nodiscard]] QRectF controlRect(int index) const {
    return pinControlRect(size(), index);
  }

  [[nodiscard]] int controlRectAt(const QPointF &position) const {
    for (int index = 0; index < 7; ++index) {
      if (controlRect(index).contains(position))
        return index;
    }
    return -1;
  }

  QImage image_;
  bool painted_ = false;
  PinExpiry expiry_;
  qreal opacity_ = 1.0;
  bool fileDragActive_ = false;
  QByteArray dragPng_;
  QImage dragPreview_;
  bool actionPending_ = false;
  QString path_;
  QString sharedPath_;
  QString savedPath_;
  quint64 previewRevision_ = 0;
  PinSnapshotFile snapshotFile_;
  PinSlotLock slotLock_;
  std::shared_ptr<PinSnapshotFile> pinDocument_;
  QFileSystemWatcher documentFiles_;
  QFutureWatcher<PinPreview> documentWatcher_;
  bool documentLoading_ = false;
  bool documentReloadPending_ = false;
  bool closing_ = false;
  QPoint position_;
  QPoint dragStart_;
  QPoint dragOffset_;
  bool dragging_ = false;
  QString toast_;
  bool hovered_ = false;
  QElapsedTimer shownClock_;
  bool pendingKeyboard_ = false;
  QPointF pendingPosition_;
  QPointF lastPointer_;
  int hoveredControl_ = -1;

};

} // namespace

int runPinnedCapture(const QString &path, PinLifetime lifetime,
                     const QString &outputName) {
  QImage image;
  {
    StartupTimingScope timing("load pin display image");
    if (PinSnapshotFile::isOwnedPath(path))
      image.load(PinSnapshotFile::thumbnailPath(path));
    if (image.isNull())
      image.load(path);
  }
  if (image.isNull()) {
    qWarning("omasnap: could not load pinned image %s", qUtf8Printable(path));
    return 1;
  }

  const QList<QScreen *> screens = QGuiApplication::screens();
  QStringList outputNames;
  outputNames.reserve(screens.size());
  for (const QScreen *screen : screens)
    outputNames.append(screen->name());

  QScreen *targetScreen = nullptr;
  if (!outputName.isEmpty()) {
    const int outputIndex = pinOutputIndex(outputName, outputNames);
    if (outputIndex < 0) {
      qCritical().noquote()
          << QStringLiteral("omasnap: pinned output is no longer available: %1")
                 .arg(outputName);
      return 1;
    }
    targetScreen = screens.at(outputIndex);
  } else {
    targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen)
      targetScreen = QGuiApplication::primaryScreen();
  }
  if (!targetScreen) {
    qCritical("omasnap: no output is available for pinned image");
    return 1;
  }

  PinWindow window(std::move(image), path,
                   pinFrameSize(targetScreen->geometry().size()), lifetime);
  if (!window.hasPinLock()) {
    qWarning("omasnap: could not lock pinned image %s", qUtf8Printable(path));
    return 1;
  }
  window.setScreen(targetScreen);
  static_cast<void>(window.winId());
  QWindow *handle = window.windowHandle();
  LayerShellQt::Window *layer =
      handle ? LayerShellQt::Window::get(handle) : nullptr;
  if (!handle || !layer) {
    qCritical("omasnap: could not create pinned layer surface");
    return 1;
  }

  handle->setScreen(targetScreen);
  layer->setScope(QStringLiteral("omasnap-pin"));
  layer->setScreen(targetScreen);
  LayerShellQt::Window::Anchors anchors;
  anchors.setFlag(LayerShellQt::Window::AnchorBottom);
  anchors.setFlag(LayerShellQt::Window::AnchorRight);
  layer->setAnchors(anchors);
  window.placeInSlot();
  layer->setExclusiveZone(0);
  layer->setDesiredSize(window.size());
  // A post-capture preview must not take keyboard focus from the app the
  // user is returning to; hovering gives it the keyboard.
  layer->setKeyboardInteractivity(
      LayerShellQt::Window::KeyboardInteractivityNone);
  layer->setActivateOnShow(false);
  layer->setLayer(LayerShellQt::Window::LayerOverlay);
  window.show();
  return QApplication::exec();
}
