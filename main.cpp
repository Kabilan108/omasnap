#include "capture.hpp"
#include "editor.hpp"
#include "image-input.hpp"
#include "signal-notifier.hpp"

#include <LayerShellQt/Window>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLockFile>
#include <QProcess>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QWindow>

namespace {
int runSavedNotification(const QString &inputPath) {
  QString imagePath;
  QString error;
  if (!resolveImagePath(inputPath, imagePath, error)) {
    qWarning().noquote() << error;
    return 1;
  }
  if (qEnvironmentVariableIsSet("OMASNAP_DISABLE_NOTIFICATIONS"))
    return 0;

  QProcess notification;
  notification.setProcessChannelMode(QProcess::SeparateChannels);
  notification.start(QStringLiteral("notify-send"),
                     {QStringLiteral("--app-name"), QStringLiteral("omasnap"),
                      QStringLiteral("--expire-time"), QStringLiteral("10000"),
                      QStringLiteral("--icon"), imagePath,
                      QStringLiteral("--action=edit=Edit screenshot"),
                      QStringLiteral("--wait"), QStringLiteral("Omasnap"),
                      QStringLiteral("Screenshot saved")});
  if (!notification.waitForStarted(2000)) {
    qWarning().noquote()
        << QStringLiteral("Could not start saved screenshot notification: %1")
               .arg(notification.errorString());
    return 1;
  }
  if (!notification.waitForFinished(-1))
    return 1;
  if (notification.exitStatus() != QProcess::NormalExit ||
      notification.exitCode() != 0)
    return 0;

  if (QString::fromUtf8(notification.readAllStandardOutput()).trimmed() ==
      QStringLiteral("edit")) {
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 {QStringLiteral("--file"), imagePath})) {
      qWarning() << "Could not reopen saved screenshot";
      return 1;
    }
  }
  return 0;
}

bool acquireInstanceLock(QLockFile &lock, bool waitForHandoff) {
  if (lock.tryLock(0))
    return true;
  if (!waitForHandoff)
    return false;

  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 2500) {
    QThread::msleep(50);
    if (lock.tryLock(0))
      return true;
  }
  return false;
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication::setApplicationName(QStringLiteral("omasnap"));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(OMASNAP_VERSION));
  QCoreApplication::setOrganizationName(QStringLiteral("Omasnap"));
  qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");
  QGuiApplication::setDesktopFileName(QStringLiteral("omasnap"));
  QApplication application(argc, argv);
  PosixSignalNotifier signalNotifier(&application);

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Native Niri screenshot and annotation overlay."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption fullscreenOption(
      QStringLiteral("capture-fullscreen"),
      QStringLiteral("Start with the entire focused output selected."));
  const QCommandLineOption windowOption(
      {QStringLiteral("capture-window"), QStringLiteral("capture-windows")},
      QStringLiteral("Start in window selection mode."));
  const QCommandLineOption regionOption(
      QStringLiteral("capture-region"),
      QStringLiteral("Start in freeform region selection mode (default)."));
  const QCommandLineOption fileOption(
      QStringLiteral("file"),
      QStringLiteral("Open an existing image in the annotation editor."),
      QStringLiteral("path"));
  const QCommandLineOption savedNotificationOption(
      QStringLiteral("saved-notification"), QString(), QStringLiteral("path"));
  parser.addOption(fullscreenOption);
  parser.addOption(windowOption);
  parser.addOption(regionOption);
  parser.addOption(fileOption);
  parser.addOption(savedNotificationOption);
  parser.addPositionalArgument(
      QStringLiteral("target"),
      QStringLiteral("Capture mode (smart, region, windows, fullscreen) or an "
                     "image path to edit."),
      QStringLiteral("[target]"));
  parser.process(application);

  const QStringList positional = parser.positionalArguments();
  const int explicitCaptureModes = parser.isSet(fullscreenOption) +
                                   parser.isSet(windowOption) +
                                   parser.isSet(regionOption);
  if (parser.isSet(savedNotificationOption)) {
    if (parser.isSet(fileOption) || explicitCaptureModes > 0 ||
        !positional.isEmpty()) {
      qCritical() << "Saved notification mode cannot be combined with other "
                     "targets";
      return 2;
    }
    return runSavedNotification(parser.value(savedNotificationOption));
  }

  if (positional.size() > 1) {
    qCritical() << "Only one capture or image target may be specified";
    return 2;
  }

  CaptureEditor::CaptureMode captureMode = CaptureEditor::CaptureMode::Region;
  if (parser.isSet(fullscreenOption))
    captureMode = CaptureEditor::CaptureMode::Fullscreen;
  else if (parser.isSet(windowOption))
    captureMode = CaptureEditor::CaptureMode::Window;

  QString fileInput = parser.value(fileOption);
  int requestedCaptureModes = explicitCaptureModes;
  if (!positional.isEmpty()) {
    if (!fileInput.isEmpty()) {
      qCritical() << "An image path may only be specified once";
      return 2;
    }
    const QString target = positional.first();
    if (target == QStringLiteral("fullscreen")) {
      captureMode = CaptureEditor::CaptureMode::Fullscreen;
      ++requestedCaptureModes;
    } else if (target == QStringLiteral("windows") ||
               target == QStringLiteral("window")) {
      captureMode = CaptureEditor::CaptureMode::Window;
      ++requestedCaptureModes;
    } else if (target == QStringLiteral("smart") ||
               target == QStringLiteral("region")) {
      captureMode = CaptureEditor::CaptureMode::Region;
      ++requestedCaptureModes;
    } else {
      fileInput = target;
    }
  }

  if (requestedCaptureModes > 1) {
    qCritical() << "Capture mode options are mutually exclusive";
    return 2;
  }
  if (!fileInput.isEmpty() && requestedCaptureModes > 0) {
    qCritical() << "An image file cannot be combined with a capture mode";
    return 2;
  }

  QString filePath;
  if (!fileInput.isEmpty()) {
    QString error;
    if (!resolveImagePath(fileInput, filePath, error)) {
      qCritical().noquote() << error;
      return 1;
    }
    captureMode = CaptureEditor::CaptureMode::File;
  }

  if (!loadCaptureFonts())
    return 1;
  application.setQuitOnLastWindowClosed(true);

  const QString runtime = secureRuntimeDirectory();
  if (runtime.isEmpty()) {
    qCritical() << "Could not create private runtime directory";
    return 1;
  }
  QLockFile instanceLock(
      QDir(runtime).filePath(QStringLiteral("omasnap.instance")));
  instanceLock.setStaleLockTime(0);
  if (!acquireInstanceLock(instanceLock, !filePath.isEmpty()))
    return 0;

  CaptureData capture;
  QString error;
  if (!filePath.isEmpty()) {
    QImage image(filePath);
    if (image.isNull()) {
      qCritical().noquote()
          << QStringLiteral("Image data could not be decoded: %1")
                 .arg(filePath);
      return 1;
    }
    capture.source = image;
    capture.preview = image;
    capture.monitor.scale = 1.0;
    capture.monitor.pixelSize = image.size();
    capture.monitor.geometry = QRect(QPoint(0, 0), image.size());
    qInfo().noquote() << QStringLiteral("Opened %1 for annotation (%2x%3)")
                             .arg(filePath)
                             .arg(image.width())
                             .arg(image.height());
  } else if (!captureFocusedMonitor(capture, error)) {
    qCritical().noquote() << error;
    sendCaptureNotification(QStringLiteral("Screenshot failed: %1").arg(error));
    return 1;
  }

  if (filePath.isEmpty())
    qInfo().noquote() << QStringLiteral("Captured %1 workspace %2")
                             .arg(capture.monitor.name)
                             .arg(capture.monitor.workspaceId);

  QScreen *targetScreen = QGuiApplication::primaryScreen();
  for (QScreen *screen : QGuiApplication::screens()) {
    if (screen->name() == capture.monitor.name) {
      targetScreen = screen;
      break;
    }
  }
  if (!targetScreen) {
    qCritical() << "No display is available for the editor";
    return 1;
  }

  CaptureEditor editor(std::move(capture), captureMode);
  editor.setScreen(targetScreen);
  editor.setGeometry(targetScreen->geometry());
  editor.winId();
  QWindow *window = editor.windowHandle();
  if (!window) {
    qCritical() << "Could not create capture overlay window";
    return 1;
  }
  LayerShellQt::Window *layerWindow = LayerShellQt::Window::get(window);
  if (!layerWindow) {
    qCritical() << "Could not create capture overlay layer";
    return 1;
  }
  layerWindow->setScope(QStringLiteral("omasnap"));
  layerWindow->setScreen(targetScreen);
  layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
  LayerShellQt::Window::Anchors anchors;
  anchors.setFlag(LayerShellQt::Window::AnchorTop);
  anchors.setFlag(LayerShellQt::Window::AnchorBottom);
  anchors.setFlag(LayerShellQt::Window::AnchorLeft);
  anchors.setFlag(LayerShellQt::Window::AnchorRight);
  layerWindow->setAnchors(anchors);
  layerWindow->setExclusiveZone(-1);
  layerWindow->setKeyboardInteractivity(
      LayerShellQt::Window::KeyboardInteractivityExclusive);
  layerWindow->setActivateOnShow(true);
  editor.show();
  editor.setFocus(Qt::ActiveWindowFocusReason);
  if (captureMode == CaptureEditor::CaptureMode::Window)
    QTimer::singleShot(0, &editor, &CaptureEditor::selectNiriWindow);

  return application.exec();
}
