// Exercise the private pin surface's real event handlers without adding a
// public window API or test hooks to the production binary. pin.cpp is not
// otherwise linked into the smoke executable.
#include "../src/pin.cpp"
#include "capture.hpp"
#include "chrome-theme.hpp"
#include "pin-file.hpp"
#include "pin-layout.hpp"
#include "pin-interaction-smoke.hpp"

#include <QByteArray>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QThreadPool>
#include <Qt>
#include <QtCore/qtestsupport_core.h>
#include <QtEnvironmentVariables>

#include <atomic>

bool runPinThemeRenderingSmoke(const QString &path, QString &error) {
  const QTemporaryDir runtime;
  if (!runtime.isValid()) {
    error = QStringLiteral("Could not isolate the pin theme fixture");
    return false;
  }
  const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
  const auto restore = qScopeGuard([&] {
    QThreadPool::globalInstance()->waitForDone();
    if (previousRuntime.isNull())
      qunsetenv("XDG_RUNTIME_DIR");
    else
      qputenv("XDG_RUNTIME_DIR", previousRuntime);
  });
  qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
  QImage source(320, 200, QImage::Format_ARGB32_Premultiplied);
  source.fill(Qt::transparent);
  const QString sourcePath = runtime.filePath(QStringLiteral("source.png"));
  if (!source.save(sourcePath)) {
    error = QStringLiteral("Could not save the pin theme fixture");
    return false;
  }
  // No show/event loop: no layer surface or interaction is requested.
  PinWindow pin(source, sourcePath, pinFrameSize(QSize(1600, 1000)),
                PinLifetime::Persistent);
  QImage card(pin.size(), QImage::Format_ARGB32_Premultiplied);
  card.fill(Qt::transparent);
  pin.render(&card);
  const bool saved = card.save(path);
  if (card.pixelColor(card.width() / 2, card.height() / 2).rgba() !=
          chromeTheme().surface.rgba() ||
      !saved) {
    error = QStringLiteral("Pin chrome surface was %1, expected %2")
                .arg(card.pixelColor(card.width() / 2, card.height() / 2).name(),
                     chromeTheme().surface.name());
    return false;
  }
  return true;
}

namespace {
bool runPinRevealSmoke(QString &error) {
  QTemporaryDir files;
  if (!files.isValid())
    return false;
  const QByteArray oldPath = qgetenv("PATH");
  const QByteArray oldRuntime = qgetenv("XDG_RUNTIME_DIR");
  const QByteArray oldScreenshots = qgetenv("OMASNAP_SCREENSHOT_DIR");
  const auto restore = qScopeGuard([&] {
    QThreadPool::globalInstance()->waitForDone();
    for (const auto &variable : {qMakePair("PATH", oldPath),
                                 qMakePair("XDG_RUNTIME_DIR", oldRuntime),
                                 qMakePair("OMASNAP_SCREENSHOT_DIR", oldScreenshots)}) {
      if (variable.second.isNull())
        qunsetenv(variable.first);
      else
        qputenv(variable.first, variable.second);
    }
  });
  const auto write = [&](const QString &name, const QByteArray &contents,
                          bool executable = false) {
    QFile file(files.filePath(name));
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(contents) == contents.size() &&
           (!executable || file.setPermissions(QFileDevice::ReadOwner |
                              QFileDevice::WriteOwner | QFileDevice::ExeOwner));
  };
  const auto read = [&](const QString &name) {
    QFile file(files.filePath(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };
  // NixOS has /bin/sh but no /bin/cat; resolve cat before PATH is isolated.
  const QByteArray cat = QStandardPaths::findExecutable(QStringLiteral("cat")).toUtf8();
  if (cat.isEmpty()) {
    error = QStringLiteral("Could not find cat for the reveal commands");
    return false;
  }
  if (!write(QStringLiteral("xdg-mime"),
             "#!/bin/sh\n" + cat + " \"${0%/*}/default-app\"\n", true) ||
      !write(QStringLiteral("busctl"),
             "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"${0%/*}/bus-args\"\n"
             "test -f \"${0%/*}/bus-ok\"\n", true) ||
      !write(QStringLiteral("xdg-open"),
             "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"${0%/*}/open-args\"\n", true) ||
      !write(QStringLiteral("wl-copy"),
             "#!/bin/sh\n" + cat + " > \"${0%/*}/clipboard\"\n", true) ||
      !write(QStringLiteral("wl-paste"),
             "#!/bin/sh\n" + cat + " \"${0%/*}/clipboard\"\n", true) ||
      !write(QStringLiteral("default-app"), "org.example.ChosenBrowser.desktop\n") ||
      !write(QStringLiteral("bus-ok"), "")) {
    error = QStringLiteral("Could not isolate the reveal commands");
    return false;
  }
  qputenv("PATH", files.path().toUtf8());
  qputenv("XDG_RUNTIME_DIR", files.path().toUtf8());
  const QString screenshots = files.filePath(QStringLiteral("shots # ' ü"));
  qputenv("OMASNAP_SCREENSHOT_DIR", screenshots.toUtf8());
  const auto drain = [] {
    for (int pass = 0; pass < 3; ++pass) {
        QThreadPool::globalInstance()->waitForDone();
      QCoreApplication::sendPostedEvents();
    }
  };
  QImage image(1600, 904, QImage::Format_RGB32);
  image.fill(Qt::cyan);
  image.setPixelColor(71, 41, Qt::red);
  const QString source = pinnedSnapshotPath(1);
  if (!image.save(source))
    return false;
  QString saved;
  {
    PinWindow pin(pinDisplayImage(image), source, QSize(200, 113), PinLifetime::Timed);
    pin.show();
    const QPoint folder = pinControlRect(pin.size(), 6).center().toPoint();
    QEnterEvent enter(folder, folder, pin.mapToGlobal(folder));
    QApplication::sendEvent(&pin, &enter);
    QTest::keyClick(&pin, Qt::Key_C);
    drain();
    if (QImage::fromData(read(QStringLiteral("clipboard")), "PNG")
            .convertToFormat(image.format()) != image) {
      error = QStringLiteral("Copy from a thumbnail lost full-resolution screenshot pixels");
      return false;
    }
    QTest::mouseClick(&pin, Qt::LeftButton, Qt::NoModifier, folder);
    drain();
    const QStringList exports = QDir(screenshots).entryList({QStringLiteral("*.png")}, QDir::Files);
    if (exports.size() != 1) {
      error = QStringLiteral("Revealing a temporary preview did not save one PNG");
      return false;
    }
    saved = QDir(screenshots).filePath(exports.constFirst());
    const QString uri = QUrl::fromLocalFile(saved).toString(QUrl::FullyEncoded);
    const QByteArray expected = QStringList{
        QStringLiteral("--user"), QStringLiteral("--timeout=3s"), QStringLiteral("--"),
        QStringLiteral("call"), QStringLiteral("org.example.ChosenBrowser"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"),
        QStringLiteral("ass"), QStringLiteral("1"), uri, QString(), QString()
    }.join(QLatin1Char('\n')).toUtf8();
    if (read(QStringLiteral("bus-args")) != expected ||
        QFileInfo::exists(files.filePath(QStringLiteral("open-args"))) ||
        QImage(saved).convertToFormat(image.format()) != image) {
      error = QStringLiteral("Reveal used the wrong browser, URI, or screenshot pixels");
      return false;
    }
    // A second reveal and Copy path share the saved file, without duplicates.
    QTest::keyClick(&pin, Qt::Key_R);
    drain();
    QTest::keyClick(&pin, Qt::Key_L);
    drain();
    if (read(QStringLiteral("clipboard")) != saved.toUtf8() ||
        QDir(screenshots).entryList({QStringLiteral("*.png")}, QDir::Files) != exports) {
      error = QStringLiteral("Reveal and Copy path did not reuse the same saved capture");
      return false;
    }
    // A drag recipient may move the source away. Copy must not silently
    // substitute the card's smaller display image for the full screenshot.
    {
      const QString moved = source + QStringLiteral(".moved");
      if (!QFile::rename(source, moved))
        return false;
      const auto restoreSource = qScopeGuard([&] { QFile::rename(moved, source); });
      if (!write(QStringLiteral("clipboard"), "a newer clipboard item"))
        return false;
      QTest::keyClick(&pin, Qt::Key_C);
      drain();
      if (read(QStringLiteral("clipboard")) != "a newer clipboard item") {
        error = QStringLiteral("Copy used a display thumbnail after its full image disappeared");
        return false;
      }
    }
    pin.close();
    drain();
  }
  if (QFileInfo::exists(source) || !QFileInfo::exists(saved)) {
    error = QStringLiteral("Closing the preview removed its revealed file");
    return false;
  }
  // Save As returns a timed runtime preview, but Reveal/Copy path must use
  // the exact user-chosen file. Further editing starts a fresh working log.
  const QString chosen = files.filePath(QStringLiteral("chosen # ' ü.png"));
  const QString savedPreview = pinnedSnapshotPath(3);
  if (!savePngFile(image, chosen, error) ||
      !savePinnedSnapshot(image, savedPreview, image.size(), error, {}, chosen))
    return false;
  {
    PinWindow pin(pinDisplayImage(image), savedPreview, QSize(200, 113), PinLifetime::Timed);
    pin.show();
    const QPoint folder = pinControlRect(pin.size(), 6).center().toPoint();
    QEnterEvent enter(folder, folder, pin.mapToGlobal(folder));
    QApplication::sendEvent(&pin, &enter);
    QTest::mouseClick(&pin, Qt::LeftButton, Qt::NoModifier, folder);
    drain();
    QTest::keyClick(&pin, Qt::Key_L);
    drain();
    if (pin.expiry_.kept() || read(QStringLiteral("clipboard")) != chosen.toUtf8() ||
        !read(QStringLiteral("bus-args")).contains(
            QUrl::fromLocalFile(chosen).toString(QUrl::FullyEncoded).toUtf8()) ||
        QDir(screenshots).entryList({QStringLiteral("*.png")}, QDir::Files).size() != 1) {
      error = QStringLiteral("Saved preview was pinned or revealed an extra saved copy");
      return false;
    }
    auto document = copyPinDocument(savedPreview, error);
    OperationLog working;
    if (!document || !loadOperationLog(operationLogPath(document->path()), working, error) ||
        !working.savedPath.isEmpty()) {
      error = QStringLiteral("Editing a saved preview retained its completed-export marker");
      return false;
    }
    pin.pinDocument_ = document;
    pin.documentFiles_.addPath(operationLogPath(document->path()));
    QTest::keyClick(&pin, Qt::Key_T);
    if (!pin.expiry_.kept()) {
      error = QStringLiteral("Could not keep the originating preview for the save test");
      return false;
    }
    document->finishSavedPreview(working, chosen);
    if (!QTest::qWaitFor([&] { return !pin.isVisible(); }, 3000)) {
      error = QStringLiteral("Saved replacement left its originating pin visible");
      return false;
    }
    drain();
  }
  if (QFile::exists(savedPreview) || QImage(chosen).convertToFormat(image.format()) != image) {
    error = QStringLiteral("Closing the saved preview damaged its durable PNG");
    return false;
  }
  // Public --pin files need not already be PNGs. Copy still offers a PNG
  // containing the full image, without changing or deleting the user file.
  const QString bitmap = files.filePath(QStringLiteral("user.bmp"));
  if (!image.save(bitmap, "BMP"))
    return false;
  {
    PinWindow pin(image, bitmap, QSize(200, 113), PinLifetime::Timed);
    pin.show();
    QTest::keyClick(&pin, Qt::Key_C, Qt::ControlModifier);
    drain();
    if (QImage::fromData(read(QStringLiteral("clipboard")), "PNG")
            .convertToFormat(image.format()) != image) {
      error = QStringLiteral("Copying a non-PNG pin lost the original pixels");
      return false;
    }
    // Drag-out offers image/png, so a non-PNG file's bytes are re-encoded.
    if (!pin.dragPng_.startsWith(QByteArrayLiteral("\x89PNG")) ||
        QImage::fromData(pin.dragPng_, "PNG").convertToFormat(image.format()) != image) {
      error = QStringLiteral("Dragging a non-PNG pin offered non-PNG bytes as image/png");
      return false;
    }
    pin.close();
    drain();
  }
  if (!QFileInfo::exists(bitmap))
    return false;
  // An action that finishes after a returned edit replaced the preview must
  // not restore the path it saved from the older image.
  {
    PinWindow pin(image, bitmap, QSize(200, 113), PinLifetime::Timed);
    std::atomic_bool release = false;
    pin.runAction([&release] {
      while (!release.load())
        QThread::msleep(2);
      return ActionResult{{}, QStringLiteral("/old/saved.png"), {}};
    }, {});
    ++pin.previewRevision_;
    release = true;
    drain();
    if (!pin.sharedPath_.isEmpty()) {
      error = QStringLiteral("A stale action restored the previous saved path");
      return false;
    }
  }
  // A changed default is queried afresh. Unsupported ShowItems opens the
  // folder via xdg-open, never an unrelated generic FileManager1 provider.
  if (!write(QStringLiteral("default-app"), "org.example.OtherBrowser.desktop\n") ||
      !QFile::remove(files.filePath(QStringLiteral("bus-ok"))) ||
      !revealFileInFolder(saved, error) ||
      !QTest::qWaitFor([&] { return !read(QStringLiteral("open-args")).isEmpty(); }, 2000) ||
      !read(QStringLiteral("bus-args")).contains("\norg.example.OtherBrowser\n") ||
      read(QStringLiteral("open-args")) !=
          (QUrl::fromLocalFile(screenshots).toString(QUrl::FullyEncoded) + QLatin1Char('\n')).toUtf8()) {
    error = QStringLiteral("Reveal did not honor a changed default or open its containing folder");
    return false;
  }
  if (sharedPinPath(saved, {}, error) != saved ||
      sharedPinPath(source, saved, error) != saved) {
    error = QStringLiteral("An existing file or retained export was copied unnecessarily");
    return false;
  }
  // Returned edits use their rendered preview, with a fresh saved copy.
  const QString edited = pinnedSnapshotPath(2) + QStringLiteral(".preview.png");
  image.fill(Qt::magenta);
  if (!image.save(edited))
    return false;
  const QString editedCopy = sharedPinPath(edited, {}, error);
  if (editedCopy.isEmpty() || editedCopy == saved ||
      QImage(editedCopy).convertToFormat(image.format()) != image) {
    error = QStringLiteral("Revealing returned edits lost the rendered image");
    return false;
  }
  if (!QFile::remove(files.filePath(QStringLiteral("xdg-open"))))
    return false;
  QString launchError;
  if (revealFileInFolder(saved, launchError) || launchError.isEmpty()) {
    error = QStringLiteral("A missing file browser launcher did not report failure");
    return false;
  }
  QString missingError;
  if (!sharedPinPath(files.filePath(QStringLiteral("missing.png")), {}, missingError).isEmpty() ||
      missingError.isEmpty()) {
    error = QStringLiteral("A missing screenshot produced a shareable path");
    return false;
  }
  return true;
}

bool runPinInteractionChecks(QString &error) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) {
    error = QStringLiteral("Could not create the pin interaction fixture");
    return false;
  }
  // Clipboard calls must not touch the developer's session. An editor child
  // exits in the smoke entry point before creating any UI.
  for (const QString &name : {QStringLiteral("wl-copy"), QStringLiteral("wl-paste")}) {
    QFile command(runtime.filePath(name));
    if (!command.open(QIODevice::WriteOnly) ||
        command.write("#!/bin/sh\nexit 1\n") < 0 ||
        !command.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner)) {
      error = QStringLiteral("Could not isolate pin interaction commands");
      return false;
    }
  }
  const QByteArray previousPath = qgetenv("PATH");
  const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
  const QByteArray previousChild = qgetenv(kPinSmokeEditorChild);
  const auto restore = qScopeGuard([&] {
    QThreadPool::globalInstance()->waitForDone();
    for (const auto &variable : {
             qMakePair("PATH", previousPath),
             qMakePair("XDG_RUNTIME_DIR", previousRuntime),
             qMakePair(kPinSmokeEditorChild, previousChild)}) {
      if (variable.second.isNull())
        qunsetenv(variable.first);
      else
        qputenv(variable.first, variable.second);
    }
  });
  qputenv("PATH", runtime.path().toUtf8());
  qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
  qputenv(kPinSmokeEditorChild, "1");

  QImage image(200, 113, QImage::Format_RGB32);
  image.fill(Qt::darkGray);
  const QString path = runtime.filePath(QStringLiteral("capture.png"));
  if (!image.save(path)) {
    error = QStringLiteral("Could not save the pin interaction fixture");
    return false;
  }
  PinWindow window(image, path, QSize(200, 113), PinLifetime::Timed);
  window.show();
  const QPoint background(100, 95);
  QEnterEvent enter(background, background, window.mapToGlobal(background));
  QApplication::sendEvent(&window, &enter);
  // The pin holds the keyboard only while the pointer moves over it: a pin
  // that maps under a resting pointer receives an enter without motion.
  auto *layer = LayerShellQt::Window::get(window.windowHandle());
  if (!layer ||
      layer->keyboardInteractivity() == LayerShellQt::Window::KeyboardInteractivityExclusive) {
    error = QStringLiteral("A pin took the keyboard without pointer motion");
    return false;
  }
  QMouseEvent hoverMove(QEvent::MouseMove, background + QPoint(3, 0),
                        window.mapToGlobal(background + QPoint(3, 0)),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&window, &hoverMove);
  if (layer->keyboardInteractivity() != LayerShellQt::Window::KeyboardInteractivityExclusive) {
    error = QStringLiteral("Hovering a pin did not give it the keyboard");
    return false;
  }
  {
    QEvent hoverLeave(QEvent::Leave);
    QApplication::sendEvent(&window, &hoverLeave);
    if (layer->keyboardInteractivity() != LayerShellQt::Window::KeyboardInteractivityNone) {
      error = QStringLiteral("Leaving a pin did not return the keyboard");
      return false;
    }
    QApplication::sendEvent(&window, &enter);
  }
  const auto drainActions = [] {
    // Completion handlers can queue document reads of their own.
    for (int pass = 0; pass < 3; ++pass) {
      QThreadPool::globalInstance()->waitForDone();
      QCoreApplication::sendPostedEvents();
    }
  };
  const auto isKept = [&] { return window.expiry_.kept(); };
  const auto expectTimed = [&](const QString &action) {
    drainActions();
    if (!isKept())
      return true;
    error = QStringLiteral("%1 implicitly pinned a timed preview").arg(action);
    return false;
  };
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, background);
  if (!expectTimed(QStringLiteral("Clicking the image")))
    return false;
  QWheelEvent wheel(background, window.mapToGlobal(background), {}, QPoint(0, 120),
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&window, &wheel);
  if (!expectTimed(QStringLiteral("Scrolling")))
    return false;
  for (const int control : {1, 2, 3, 4, 6}) {
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      pinControlRect(window.size(), control).center().toPoint());
    if (!expectTimed(QStringLiteral("Using control %1").arg(control)))
      return false;
  }
  // Edit and Reveal open another window that needs the keyboard, even while
  // the pointer stays on the pin.
  for (const int control : {3, 6}) {
    QApplication::sendEvent(&window, &hoverMove);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      pinControlRect(window.size(), control).center().toPoint());
    if (layer->keyboardInteractivity() == LayerShellQt::Window::KeyboardInteractivityExclusive) {
      error = QStringLiteral("Control %1 kept the keyboard from the window it opened")
                  .arg(control);
      return false;
    }
    drainActions();
  }
  for (const Qt::Key key : {Qt::Key_C, Qt::Key_L, Qt::Key_E, Qt::Key_R}) {
    QApplication::sendEvent(&window, &enter);
    QTest::keyClick(&window, key);
    if (!expectTimed(QStringLiteral("Using shortcut %1").arg(static_cast<int>(key))))
      return false;
  }
  QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier);
  if (!expectTimed(QStringLiteral("Ctrl+C")))
    return false;

  // Edit opens the private document with the pin's output and keeps this pin
  // open so the edit can return to it.
  if (!window.pinDocument_ || !window.isVisible() ||
      !window.documentFiles_.files().contains(
          operationLogPath(window.pinDocument_->path()))) {
    error = QStringLiteral("Edit did not keep the pin watching its document");
    return false;
  }

  // Moving the pin with the pointer keeps it; a click without movement does
  // not (checked above).
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, background);
  QMouseEvent drag(QEvent::MouseMove, background + QPoint(40, 30),
                   window.mapToGlobal(background + QPoint(40, 30)), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&window, &drag);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier,
                      background + QPoint(40, 30));
  if (!isKept()) {
    error = QStringLiteral("Dragging the preview did not keep it");
    return false;
  }
  QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier);
  if (!expectTimed(QStringLiteral("Unpinning after a drag")))
    return false;

  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                    pinControlRect(window.size(), 5).center().toPoint());
  if (!isKept()) {
    error = QStringLiteral("The explicit pin button did not keep the preview");
    return false;
  }
  QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier);
  if (!expectTimed(QStringLiteral("Unpinning with Ctrl+P")))
    return false;
  QTest::keyClick(&window, Qt::Key_P, Qt::ControlModifier);
  if (!isKept()) {
    error = QStringLiteral("Ctrl+P did not keep the preview");
    return false;
  }
  QApplication::sendEvent(&window, &enter);
  QTest::keyClick(&window, Qt::Key_T);
  if (!expectTimed(QStringLiteral("Unpinning with T")))
    return false;
  QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier);
  if (!expectTimed(QStringLiteral("Ctrl+T")))
    return false;
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&window, &leave);
  QTest::keyClick(&window, Qt::Key_T);
  if (!expectTimed(QStringLiteral("T without hovering")))
    return false;
  QApplication::sendEvent(&window, &enter);
  QTest::keyClick(&window, Qt::Key_T);
  if (!isKept()) {
    error = QStringLiteral("T did not keep the hovered preview");
    return false;
  }
  QKeyEvent repeat(QEvent::KeyPress, Qt::Key_T, Qt::NoModifier,
                   QStringLiteral("t"), true);
  QApplication::sendEvent(&window, &repeat);
  if (!isKept()) {
    error = QStringLiteral("Holding T repeatedly toggled the preview pin");
    return false;
  }
  // Moving onto a pin well after it mapped takes the keyboard at once.
  QTest::qWait(kMapEnterMs + 50);
  {
    QEvent hoverLeave(QEvent::Leave);
    QApplication::sendEvent(&window, &hoverLeave);
    QApplication::sendEvent(&window, &enter);
    if (layer->keyboardInteractivity() != LayerShellQt::Window::KeyboardInteractivityExclusive) {
      error = QStringLiteral("Moving onto a mapped pin did not give it the keyboard");
      return false;
    }
  }
  QTest::keyClick(&window, Qt::Key_X);
  if (window.isVisible()) {
    error = QStringLiteral("X did not close the pin");
    return false;
  }
  drainActions();
  return true;
}
} // namespace

bool runPinInteractionSmoke(QString &error) {
  return runPinRevealSmoke(error) && runPinInteractionChecks(error);
}
