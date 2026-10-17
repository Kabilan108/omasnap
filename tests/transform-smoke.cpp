/** @fileoverview Tests Niri discovery and clean capture command integration. */
#include "transform-smoke.hpp"

#include "capture.hpp"
#include "editor.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {
bool writeExecutable(const QString &path, const QByteArray &contents) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(contents) != contents.size())
    return false;
  file.close();
  return QFile::setPermissions(path, QFileDevice::ReadOwner |
                                         QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner);
}
} // namespace

bool runTransformSmoke(QString &error) {
  QTemporaryDir fakeCommands;
  if (!fakeCommands.isValid()) {
    error = QStringLiteral("Could not create Niri capture-test directory");
    return false;
  }

  const QString outputImage =
      QDir(fakeCommands.path()).filePath(QStringLiteral("output.ppm"));
  QImage output(450, 300, QImage::Format_RGB32);
  output.fill(QColor(QStringLiteral("#123456")));
  if (!output.save(outputImage, "PPM")) {
    error = QStringLiteral("Could not create output capture fixture");
    return false;
  }

  const QString windowImage =
      QDir(fakeCommands.path()).filePath(QStringLiteral("window.png"));
  QImage window(240, 120, QImage::Format_RGB32);
  window.fill(QColor(QStringLiteral("#654321")));
  if (!window.save(windowImage, "PNG")) {
    error = QStringLiteral("Could not create window capture fixture");
    return false;
  }

  const QString clipboard =
      QDir(fakeCommands.path()).filePath(QStringLiteral("clipboard"));
  const QString clipboardType =
      QDir(fakeCommands.path()).filePath(QStringLiteral("clipboard-type"));
  const QString screenshotInvoked =
      QDir(fakeCommands.path()).filePath(QStringLiteral("screenshot-invoked"));
  const QString screenshotRelease =
      QDir(fakeCommands.path()).filePath(QStringLiteral("screenshot-release"));
  {
    QFile payload(clipboard);
    QFile type(clipboardType);
    if (!payload.open(QIODevice::WriteOnly) || payload.write("before") != 6 ||
        !type.open(QIODevice::WriteOnly) ||
        type.write("text/plain;charset=utf-8") != 24) {
      error = QStringLiteral("Could not prepare clipboard fixture");
      return false;
    }
  }

  const QString bash = QStandardPaths::findExecutable(QStringLiteral("bash"));
  if (bash.isEmpty()) {
    error = QStringLiteral("Could not find bash for Niri capture-test commands");
    return false;
  }
  const QByteArray scriptHeader =
      QByteArrayLiteral("#!") + bash.toUtf8() + QByteArrayLiteral("\n");
  const QByteArray niriScript = scriptHeader + QByteArrayLiteral(
      "set -euo pipefail\n"
      "if [[ \"${2:-}\" == --json ]]; then shift 2; else shift; fi\n"
      "case \"${1:-}\" in\n"
      "  focused-output)\n"
      "    printf '%s\\n' '{\"name\":\"TEST\",\"logical\":{\"x\":10,\"y\":20,\"width\":300,\"height\":200,\"scale\":1.5}}'\n"
      "    ;;\n"
      "  workspaces)\n"
      "    printf '%s\\n' '[{\"id\":7,\"output\":\"TEST\",\"is_focused\":true,\"is_active\":true}]'\n"
      "    ;;\n"
      "  outputs)\n"
      "    printf '%s\\n' '{\"TEST\":{\"logical\":{\"x\":10,\"y\":20,\"width\":300,\"height\":200,\"scale\":1.5}}}'\n"
      "    ;;\n"
      "  focused-window)\n"
      "    printf '%s\\n' '{\"id\":99,\"workspace_id\":7}'\n"
      "    ;;\n"
      "  action)\n"
      "    shift 2\n"
      "    printf 'invoked' > \"$OMASNAP_TEST_SCREENSHOT_INVOKED\"\n"
      "    if [[ \"${OMASNAP_TEST_BLOCK_SCREENSHOT:-0}\" == 1 ]]; then while [[ ! -e \"$OMASNAP_TEST_SCREENSHOT_RELEASE\" ]]; do sleep 0.02; done; fi\n"
      "    target=\n"
      "    while (($#)); do if [[ \"$1\" == --path ]]; then target=$2; shift 2; else shift; fi; done\n"
      "    cp \"$OMASNAP_TEST_WINDOW_IMAGE\" \"$target\"\n"
      "    cp \"$OMASNAP_TEST_WINDOW_IMAGE\" \"$OMASNAP_TEST_CLIPBOARD\"\n"
      "    printf 'image/png' > \"$OMASNAP_TEST_CLIPBOARD_TYPE\"\n"
      "    ;;\n"
      "  *) exit 2 ;;\n"
      "esac\n");
  const QByteArray grimScript = scriptHeader + QByteArrayLiteral(
      "set -euo pipefail\n"
      "cat \"$OMASNAP_TEST_OUTPUT_IMAGE\"\n");
  const QByteArray pasteScript = scriptHeader + QByteArrayLiteral(
      "set -euo pipefail\n"
      "if [[ \"${1:-}\" == --list-types ]]; then\n"
      "  if [[ \"${OMASNAP_TEST_CLIPBOARD_LIST_FAILURE:-0}\" == 1 ]]; then printf 'clipboard unavailable\\n' >&2; exit 2; fi\n"
      "  cat \"$OMASNAP_TEST_CLIPBOARD_TYPE\"; printf '\\n'\n"
      "else cat \"$OMASNAP_TEST_CLIPBOARD\"; fi\n");
  const QByteArray copyScript = scriptHeader + QByteArrayLiteral(
      "set -euo pipefail\n"
      "if [[ \"${1:-}\" == --clear ]]; then : > \"$OMASNAP_TEST_CLIPBOARD\"; : > \"$OMASNAP_TEST_CLIPBOARD_TYPE\"; exit 0; fi\n"
      "type=text/plain\n"
      "if [[ \"${1:-}\" == --type ]]; then type=$2; fi\n"
      "cat > \"$OMASNAP_TEST_CLIPBOARD\"\n"
      "printf '%s' \"$type\" > \"$OMASNAP_TEST_CLIPBOARD_TYPE\"\n");

  if (!writeExecutable(QDir(fakeCommands.path()).filePath(QStringLiteral("niri")),
                       niriScript) ||
      !writeExecutable(QDir(fakeCommands.path()).filePath(QStringLiteral("grim")),
                       grimScript) ||
      !writeExecutable(
          QDir(fakeCommands.path()).filePath(QStringLiteral("wl-paste")),
          pasteScript) ||
      !writeExecutable(QDir(fakeCommands.path()).filePath(QStringLiteral("wl-copy")),
                       copyScript)) {
    error = QStringLiteral("Could not create Niri capture-test commands");
    return false;
  }

  const QByteArray originalPath = qgetenv("PATH");
  qputenv("PATH", fakeCommands.path().toUtf8() + ':' + originalPath);
  qputenv("OMASNAP_TEST_OUTPUT_IMAGE", outputImage.toUtf8());
  qputenv("OMASNAP_TEST_WINDOW_IMAGE", windowImage.toUtf8());
  qputenv("OMASNAP_TEST_CLIPBOARD", clipboard.toUtf8());
  qputenv("OMASNAP_TEST_CLIPBOARD_TYPE", clipboardType.toUtf8());
  qputenv("OMASNAP_TEST_SCREENSHOT_INVOKED", screenshotInvoked.toUtf8());
  qputenv("OMASNAP_TEST_SCREENSHOT_RELEASE", screenshotRelease.toUtf8());
  const auto restoreEnvironment = [&originalPath] {
    qputenv("PATH", originalPath);
    qunsetenv("OMASNAP_TEST_OUTPUT_IMAGE");
    qunsetenv("OMASNAP_TEST_WINDOW_IMAGE");
    qunsetenv("OMASNAP_TEST_CLIPBOARD");
    qunsetenv("OMASNAP_TEST_CLIPBOARD_TYPE");
    qunsetenv("OMASNAP_TEST_SCREENSHOT_INVOKED");
    qunsetenv("OMASNAP_TEST_SCREENSHOT_RELEASE");
    qunsetenv("OMASNAP_TEST_BLOCK_SCREENSHOT");
    qunsetenv("OMASNAP_TEST_CLIPBOARD_LIST_FAILURE");
  };

  CaptureData focused;
  if (!captureFocusedMonitor(focused, error) ||
      focused.monitor.name != QStringLiteral("TEST") ||
      focused.monitor.geometry != QRect(10, 20, 300, 200) ||
      focused.monitor.pixelSize != QSize(450, 300) ||
      !qFuzzyCompare(focused.monitor.scale, 1.5) ||
      focused.monitor.workspaceId != 7 || focused.source != output ||
      focused.previewSize != QSize(300, 200)) {
    if (error.isEmpty())
      error = QStringLiteral("Focused Niri output capture was incorrect");
    restoreEnvironment();
    return false;
  }

  QString focusedOutput;
  if (!queryFocusedOutputName(focusedOutput, error) ||
      focusedOutput != QStringLiteral("TEST")) {
    if (error.isEmpty())
      error = QStringLiteral("Focused Niri output name was incorrect");
    restoreEnvironment();
    return false;
  }

  CaptureData selected;
  if (!captureFocusedWindow(selected, error) || selected.source != window ||
      selected.previewSize != QSize(160, 80) ||
      selected.monitor.name != QStringLiteral("TEST")) {
    if (error.isEmpty())
      error = QStringLiteral("Selected Niri window capture was incorrect");
    restoreEnvironment();
    return false;
  }
  QFile restoredPayload(clipboard);
  QFile restoredType(clipboardType);
  if (!restoredPayload.open(QIODevice::ReadOnly) ||
      restoredPayload.readAll() != QByteArrayLiteral("before") ||
      !restoredType.open(QIODevice::ReadOnly) ||
      restoredType.readAll() != QByteArrayLiteral("text/plain;charset=utf-8")) {
    error = QStringLiteral("Window capture did not restore the clipboard");
    restoreEnvironment();
    return false;
  }

  restoredPayload.close();
  restoredType.close();

  // A clean window capture keeps the configured default backdrop as its
  // first operation, so replaying later annotations does not drop it.
  {
    const QByteArray oldConfigHome = qgetenv("XDG_CONFIG_HOME");
    const QString configDir =
        QDir(fakeCommands.path()).filePath(QStringLiteral("config/omasnap"));
    QFile config(QDir(configDir).filePath(QStringLiteral("omasnap.conf")));
    if (!QDir().mkpath(configDir) || !config.open(QIODevice::WriteOnly) ||
        config.write("[background]\ndefault = slate\n") < 0) {
      error = QStringLiteral("Could not write the backdrop config fixture");
      restoreEnvironment();
      return false;
    }
    config.close();
    qputenv("XDG_CONFIG_HOME",
            QDir(fakeCommands.path()).filePath(QStringLiteral("config")).toUtf8());
    CaptureEditor editor(focused, CaptureEditor::CaptureMode::Smart,
                         QuickOutputMode::None);
    editor.setSuppressSnapshots(true);
    editor.setFocusedWindowTarget(
        QByteArrayLiteral("{\"id\":99,\"workspace_id\":7,\"app_id\":\"org.test.App\"}"));
    editor.captureFocusedNiriWindow();
    editor.waitForWindowCapture();
    QApplication::processEvents();
    if (oldConfigHome.isNull())
      qunsetenv("XDG_CONFIG_HOME");
    else
      qputenv("XDG_CONFIG_HOME", oldConfigHome);
    const auto &ops = editor.operationLog();
    const bool seeded =
        !ops.isEmpty() && ops.constFirst().type == Operation::Type::Background &&
        ops.constFirst().background == BackgroundStyle::Slate &&
        editor.operationIndex() >= 1;
    if (!seeded || editor.captureData().appId != QStringLiteral("org.test.App") ||
        editor.captureData().source != window) {
      error = QStringLiteral("Window capture dropped its backdrop, app id, or pixels");
      restoreEnvironment();
      return false;
    }
    editor.close();
  }

  QFile::remove(screenshotInvoked);
  QFile::remove(screenshotRelease);
  qputenv("OMASNAP_TEST_BLOCK_SCREENSHOT", QByteArrayLiteral("1"));
  QElapsedTimer shutdown;
  {
    auto editor = std::make_unique<CaptureEditor>(focused);
    editor->setFocusedWindowTarget(
        QByteArrayLiteral("{\"id\":99,\"workspace_id\":7}"));
    editor->captureFocusedNiriWindow();
    QElapsedTimer started;
    started.start();
    while (!QFile::exists(screenshotInvoked) && started.elapsed() < 5000) {
      QApplication::processEvents();
      QThread::msleep(2);
    }
    if (!QFile::exists(screenshotInvoked)) {
      error = QStringLiteral("Focused-window capture did not reach Niri action");
      restoreEnvironment();
      return false;
    }
    std::atomic_bool releaseOk = false;
    std::jthread release([&releaseOk, screenshotRelease] {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      QFile marker(screenshotRelease);
      releaseOk = marker.open(QIODevice::WriteOnly);
    });
    shutdown.start();
    // Destruction must wait for the blocked action and clipboard restoration
    // rather than leaving work behind after the instance lock.
    editor.reset();
    release.join();
    if (!releaseOk) {
      error = QStringLiteral("Could not release blocked window capture fixture");
      restoreEnvironment();
      return false;
    }
  }
  if (shutdown.elapsed() < 50) {
    error = QStringLiteral("Editor shutdown did not drain window capture");
    restoreEnvironment();
    return false;
  }
  qputenv("OMASNAP_TEST_BLOCK_SCREENSHOT", QByteArrayLiteral("0"));
  QFile drainedPayload(clipboard);
  QFile drainedType(clipboardType);
  if (!drainedPayload.open(QIODevice::ReadOnly) ||
      drainedPayload.readAll() != QByteArrayLiteral("before") ||
      !drainedType.open(QIODevice::ReadOnly) ||
      drainedType.readAll() != QByteArrayLiteral("text/plain;charset=utf-8")) {
    error = QStringLiteral("Shutdown returned before clipboard restoration");
    restoreEnvironment();
    return false;
  }

  QFile::remove(screenshotInvoked);
  qputenv("OMASNAP_TEST_CLIPBOARD_LIST_FAILURE", QByteArrayLiteral("1"));
  CaptureData cancelled;
  QString cancelError;
  if (captureFocusedWindow(cancelled, cancelError) || cancelError.isEmpty() ||
      QFile::exists(screenshotInvoked)) {
    error = QStringLiteral(
        "Clipboard snapshot failure did not abort Niri window capture");
    restoreEnvironment();
    return false;
  }

  restoreEnvironment();
  return true;
}
