/** @fileoverview Saved-capture notifications, with and without --notify. */
#include "notify-smoke.hpp"
#include "capture.hpp"
#include "editor.hpp"
#include "output-config.hpp"
#include "pin-file.hpp"
#include "recent-snaps.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtCore/qtenvironmentvariables.h>
#include <QtTest/qtestkeyboard.h>

#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

namespace {
using Launch = std::pair<QString, QStringList>;

class EnvironmentGuard {
public:
  explicit EnvironmentGuard(std::initializer_list<const char *> names) {
    for (const char *name : names)
      saved_.emplace_back(name, qgetenv(name));
  }
  ~EnvironmentGuard() {
    for (const auto &[name, value] : saved_)
      value.isNull() ? qunsetenv(name) : qputenv(name, value);
  }
  EnvironmentGuard(const EnvironmentGuard &) = delete;
  EnvironmentGuard &operator=(const EnvironmentGuard &) = delete;

private:
  std::vector<std::pair<const char *, QByteArray>> saved_;
};

template <typename Predicate> bool waitUntil(Predicate ready, int timeoutMs = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeoutMs) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  return ready();
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &contents, bool executable = false) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write(contents) != contents.size())
    return false;
  file.close();
  return !executable ||
         file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                             QFileDevice::ExeOwner);
}

QStringList lines(const QByteArray &contents) {
  QStringList result = QString::fromUtf8(contents).split(QLatin1Char('\n'));
  if (!result.isEmpty() && result.last().isEmpty())
    result.removeLast();
  return result;
}

QString sizeDetails(const QString &path, QSize size) {
  const QLocale locale;
  return QStringLiteral("%1 × %2 · %3")
      .arg(locale.toString(size.width()), locale.toString(size.height()),
           locale.formattedDataSize(QFileInfo(path).size(), 1,
                                    QLocale::DataSizeSIFormat));
}

/** The helper's notify-send argv in both modes, and what each action runs. */
bool checkHelper(const QDir &fakes, const QString &image, QString &error) {
  const QString reply = fakes.filePath(QStringLiteral("notify-reply"));
  const QString recentId = QUuid::createUuid().toString(QUuid::Id128);
  std::vector<Launch> launches;
  const auto launcher = [&](const QString &program, const QStringList &arguments) {
    launches.emplace_back(program, arguments);
    return true;
  };
  const QString app = QCoreApplication::applicationFilePath();

  qunsetenv("OMASNAP_NOTIFY");
  if (!writeFile(reply, "edit\n") ||
      runSavedNotification(image, QStringLiteral("Screenshot saved and copied"),
                           recentId, launcher) != 0) {
    error = QStringLiteral("The saved notification helper failed without --notify");
    return false;
  }
  const QStringList plain{
      QStringLiteral("--app-name"), QStringLiteral("omasnap"),
      QStringLiteral("--expire-time"), QStringLiteral("7000"),
      QStringLiteral("--icon"), image,
      QStringLiteral("--action=edit=Edit screenshot"), QStringLiteral("--wait"),
      QStringLiteral("Omasnap"), QStringLiteral("Screenshot saved and copied")};
  const QStringList plainSent = lines(readFile(fakes.filePath(QStringLiteral("notify-args"))));
  if (plainSent != plain) {
    error = QStringLiteral("Saved notification argv is wrong: %1")
                .arg(plainSent.join(QStringLiteral(" | ")));
    return false;
  }
  // Edit restores the capture's layers, with the saved PNG as the fallback.
  const Launch editRecent{app, {QStringLiteral("--recent"), recentId,
                                QStringLiteral("--file"), image}};
  if (launches.size() != 1 || launches.front() != editRecent) {
    error = QStringLiteral("Edit did not reopen the recent capture with its file fallback");
    return false;
  }

  qputenv("OMASNAP_NOTIFY", "1");
  launches.clear();
  if (runSavedNotification(image, QStringLiteral("Screenshot saved"), {}, launcher) != 0) {
    error = QStringLiteral("The saved notification helper failed under --notify");
    return false;
  }
  const QStringList announced{
      QStringLiteral("--app-name"), QStringLiteral("omasnap"),
      QStringLiteral("--expire-time"), QStringLiteral("7000"),
      QStringLiteral("--icon"), image,
      QStringLiteral("--action=open=Open"), QStringLiteral("--action=edit=Edit"),
      QStringLiteral("--wait"), QStringLiteral("Screenshot saved"),
      sizeDetails(image, QImage(image).size())};
  const QStringList announcedSent =
      lines(readFile(fakes.filePath(QStringLiteral("notify-args"))));
  if (announcedSent != announced) {
    error = QStringLiteral("--notify argv is wrong: %1")
                .arg(announcedSent.join(QStringLiteral(" | ")));
    return false;
  }
  const Launch editFile{app, {QStringLiteral("--file"), image}};
  if (launches.size() != 1 || launches.front() != editFile) {
    error = QStringLiteral("Edit without a recent capture did not open the saved file");
    return false;
  }

  // Open runs xdg-open as an argv list; the real launch path is exercised.
  const QString opened = fakes.filePath(QStringLiteral("open-args"));
  QFile::remove(opened);
  if (!writeFile(reply, "open\n") ||
      runSavedNotification(image, QStringLiteral("Screenshot saved"), recentId) != 0 ||
      !waitUntil([&] { return QFileInfo(opened).size() > 0; }) ||
      lines(readFile(opened)) != QStringList{image}) {
    error = QStringLiteral("Open did not run xdg-open on the saved file");
    return false;
  }

  // Dismissing the notification runs nothing.
  launches.clear();
  QFile::remove(reply);
  if (runSavedNotification(image, QStringLiteral("Screenshot saved"), recentId,
                           launcher) != 0 ||
      !launches.empty()) {
    error = QStringLiteral("A dismissed notification still ran an action");
    return false;
  }
  return true;
}

/** main() turns --notify into OMASNAP_NOTIFY, which every child inherits. */
bool checkBinaryPropagation(const QDir &fakes, const QString &image, QString &error) {
  const QString omasnap =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("omasnap"));
  if (!QFileInfo(omasnap).isExecutable()) {
    error = QStringLiteral("The omasnap binary is not built beside the smoke suite");
    return false;
  }
  const QString recentId = QUuid::createUuid().toString(QUuid::Id128);
  for (const bool notify : {true, false}) {
    QFile::remove(fakes.filePath(QStringLiteral("notify-args")));
    QFile::remove(fakes.filePath(QStringLiteral("open-args")));
    if (!writeFile(fakes.filePath(QStringLiteral("notify-reply")),
                   notify ? "open\n" : "dismissed\n"))
      return false;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), fakes.path());
    environment.remove(QStringLiteral("OMASNAP_NOTIFY"));
    environment.remove(QStringLiteral("OMASNAP_DISABLE_NOTIFICATIONS"));
    QProcess helper;
    helper.setProcessEnvironment(environment);
    QStringList arguments{QStringLiteral("--saved-notification"), image,
                          QStringLiteral("--headline"),
                          QStringLiteral("Screenshot saved and copied"),
                          QStringLiteral("--recent"), recentId};
    if (notify)
      arguments.prepend(QStringLiteral("--notify"));
    helper.start(omasnap, arguments);
    if (!helper.waitForFinished(15000) || helper.exitStatus() != QProcess::NormalExit ||
        helper.exitCode() != 0) {
      error = QStringLiteral("omasnap --saved-notification failed: %1")
                  .arg(QString::fromUtf8(helper.readAllStandardError()));
      return false;
    }
    const QStringList sent = lines(readFile(fakes.filePath(QStringLiteral("notify-args"))));
    const QByteArray inherited = readFile(fakes.filePath(QStringLiteral("notify-env")));
    if (inherited != (notify ? "1" : "") ||
        sent.contains(QStringLiteral("--action=open=Open")) != notify ||
        sent.value(sent.size() - (notify ? 2 : 1)) !=
            QStringLiteral("Screenshot saved and copied")) {
      error = QStringLiteral("--notify did not reach the notification (notify=%1): %2")
                  .arg(notify)
                  .arg(sent.join(QStringLiteral(" | ")));
      return false;
    }
    if (notify && (!waitUntil([&] {
                     return QFileInfo(fakes.filePath(QStringLiteral("open-args"))).size() > 0;
                   }) ||
                   lines(readFile(fakes.filePath(QStringLiteral("open-args")))) !=
                       QStringList{image})) {
      error = QStringLiteral("The binary's Open action did not open the saved file");
      return false;
    }
  }
  return true;
}

/** --recent reopens the shelf entry, else falls back to --file, else fails. */
bool checkRecentFallback(const QDir &work, QString &error) {
  qputenv("OMASNAP_RECENT_DIR", work.filePath(QStringLiteral("recent")).toUtf8());
  QImage source(40, 30, QImage::Format_ARGB32_Premultiplied);
  source.fill(Qt::darkCyan);
  OperationLog log;
  log.previewSize = source.size();
  log.recentId = QUuid::createUuid().toString(QUuid::Id128);
  if (!recordRecentSnap(source, log, source, error))
    return false;
  const QString fallback = work.filePath(QStringLiteral("saved.png"));
  std::optional<RecentSnap> recent;
  if (!resolveRecentTarget(log.recentId, fallback, recent, error) || !recent ||
      QImage(recent->sourcePath).isNull() || recent->logPath.isEmpty()) {
    error = QStringLiteral("--recent did not find a shelved capture: %1").arg(error);
    return false;
  }
  const QString dropped = QUuid::createUuid().toString(QUuid::Id128);
  if (!resolveRecentTarget(dropped, fallback, recent, error) || recent ||
      !error.isEmpty()) {
    error = QStringLiteral("--recent did not fall back to --file: %1").arg(error);
    return false;
  }
  if (resolveRecentTarget(dropped, {}, recent, error) || recent || error.isEmpty()) {
    error = QStringLiteral("--recent without a shelved capture or file was accepted");
    return false;
  }
  error.clear();
  return true;
}

} // namespace

/** Under --notify a saved capture launches the helper instead of a preview. */
bool runNotifyEditorSmoke(const QString &workPath, QString &error) {
  const QDir work(workPath);
  const QString clipboard = work.filePath(QStringLiteral("clipboard.png"));
  qputenv("OMASNAP_TEST_PIN_CLIPBOARD", clipboard.toUtf8());
  qunsetenv("OMASNAP_TEST_COPY_FAIL");
  // Each check saves into its own directory, so its file is the only one.
  QString saved;
  const auto saveInto = [&](const QString &name) {
    saved = work.filePath(name);
    qputenv("OMASNAP_SCREENSHOT_DIR", saved.toUtf8());
  };
  qputenv("OMASNAP_RECENT_DIR", work.filePath(QStringLiteral("editor-recent")).toUtf8());
  qputenv("XDG_CONFIG_HOME", work.filePath(QStringLiteral("config")).toUtf8());
  qputenv("OMASNAP_NOTIFY", "1");

  CaptureData capture;
  capture.monitor.geometry = {0, 0, 400, 300};
  capture.monitor.pixelSize = {800, 600};
  capture.monitor.scale = 2;
  capture.source = QImage(800, 600, QImage::Format_ARGB32_Premultiplied);
  capture.source.fill(QColor(QStringLiteral("#345678")));
  capture.previewSize = {400, 300};

  QStringList previews;
  const auto cleanup = qScopeGuard([&] {
    for (const QString &path : std::as_const(previews)) {
      const PinSnapshotFile owned(path);
    }
  });
  using Mode = CaptureEditor::CaptureMode;
  const auto expectNotification = [&](const std::vector<Launch> &launches,
                                       const QString &headline,
                                       const QString &recentId, const QString &what) {
    const QStringList pngs = QDir(saved).entryList({QStringLiteral("*.png")}, QDir::Files);
    const QStringList expected{
        QStringLiteral("--saved-notification"),
        pngs.size() == 1 ? QDir(saved).filePath(pngs.first()) : QString(),
        QStringLiteral("--headline"), headline, QStringLiteral("--recent"), recentId};
    if (launches.size() != 1 ||
        launches.front().first != QCoreApplication::applicationFilePath() ||
        launches.front().second != expected) {
      QStringList seen;
      for (const auto &[program, arguments] : launches)
        seen << arguments.join(QLatin1Char(' '));
      error = QStringLiteral("%1 under --notify launched %2").arg(what, seen.join(QStringLiteral(" ; ")));
      return false;
    }
    return true;
  };

  // A fresh autosaved capture: no preview, only the notification.
  saveInto(QStringLiteral("saved-fresh"));
  {
    CaptureEditor editor(capture, Mode::Fullscreen, QuickOutputMode::CopyAndPreview);
    std::vector<Launch> launches;
    editor.setProcessLauncherForTest([&](const QString &program, const QStringList &arguments) {
      launches.emplace_back(program, arguments);
      return true;
    });
    editor.show();
    editor.waitForExport();
    if (!waitUntil([&] { return !editor.isVisible(); }) ||
        !expectNotification(launches, QStringLiteral("Screenshot saved and copied"),
                            editor.recentId_, QStringLiteral("An autosaved capture")))
      return false;
  }

  // Saving from the editor: the notification replaces the saved preview.
  saveInto(QStringLiteral("saved-edited"));
  {
    CaptureEditor editor(capture, Mode::File);
    editor.setSuppressSnapshots(true);
    std::vector<Launch> launches;
    editor.setProcessLauncherForTest([&](const QString &program, const QStringList &arguments) {
      launches.emplace_back(program, arguments);
      return true;
    });
    editor.resize(400, 300);
    editor.show();
    QTest::keyClick(&editor, Qt::Key_S, Qt::ControlModifier);
    editor.waitForExport();
    if (!waitUntil([&] { return !editor.isVisible(); }) ||
        !expectNotification(launches, QStringLiteral("Screenshot saved"),
                            editor.recentId_, QStringLiteral("Saving from the editor")))
      return false;
  }

  // Save As announces the chosen file.
  {
    CaptureEditor editor(capture, Mode::File);
    editor.setSuppressSnapshots(true);
    std::vector<Launch> launches;
    editor.setProcessLauncherForTest([&](const QString &program, const QStringList &arguments) {
      launches.emplace_back(program, arguments);
      return true;
    });
    editor.resize(400, 300);
    editor.show();
    const QString chosen = work.filePath(QStringLiteral("chosen.png"));
    editor.saveAsToPath(chosen);
    const QStringList expected{QStringLiteral("--saved-notification"), chosen,
                               QStringLiteral("--headline"),
                               QStringLiteral("Screenshot saved"),
                               QStringLiteral("--recent"), editor.recentId_};
    if (!waitUntil([&] { return !editor.busy_ && !editor.isVisible(); }) ||
        launches.size() != 1 || launches.front().second != expected ||
        QImage(chosen).isNull()) {
      error = QStringLiteral("Save As under --notify did not announce the chosen file");
      return false;
    }
  }

  // Copy-only output has no file to open, so it keeps its timed preview.
  if (!QDir().mkpath(QFileInfo(defaultConfigPath()).absolutePath()) ||
      !writeFile(defaultConfigPath(), "[output]\nautosave = false\n"))
    return false;
  {
    CaptureEditor editor(capture, Mode::Fullscreen, QuickOutputMode::CopyAndPreview);
    std::vector<Launch> launches;
    editor.setProcessLauncherForTest([&](const QString &program, const QStringList &arguments) {
      launches.emplace_back(program, arguments);
      if (arguments.size() == 2 && arguments.first() == QStringLiteral("--preview"))
        previews << arguments.last();
      return true;
    });
    editor.show();
    editor.waitForExport();
    if (!waitUntil([&] { return !editor.isVisible(); }) || launches.size() != 1 ||
        launches.front().second.value(0) != QStringLiteral("--preview")) {
      error = QStringLiteral("Copy-only output under --notify lost its preview");
      return false;
    }
  }
  return true;
}

bool runNotifySmoke(QString &error) {
  QTemporaryDir work;
  if (!work.isValid()) {
    error = QStringLiteral("Could not create the notification test directory");
    return false;
  }
  const QDir root(work.path());
  const QDir fakes(root.filePath(QStringLiteral("fakes")));
  // NixOS has /bin/sh but no /bin/cat; resolve cat before PATH is isolated.
  const QByteArray cat = QStandardPaths::findExecutable(QStringLiteral("cat")).toUtf8();
  if (cat.isEmpty() || !QDir().mkpath(fakes.path()) ||
      !writeFile(fakes.filePath(QStringLiteral("notify-send")),
                 "#!/bin/sh\n"
                 "printf '%s\\n' \"$@\" > \"${0%/*}/notify-args\"\n"
                 "printf '%s' \"$OMASNAP_NOTIFY\" > \"${0%/*}/notify-env\"\n"
                 "if [ -f \"${0%/*}/notify-reply\" ]; then exec " +
                     cat + " \"${0%/*}/notify-reply\"; fi\n",
                 true) ||
      !writeFile(fakes.filePath(QStringLiteral("xdg-open")),
                 "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"${0%/*}/open-args\"\n", true) ||
      !writeFile(fakes.filePath(QStringLiteral("wl-copy")),
                 "#!/bin/sh\nexec " + cat + " > \"$OMASNAP_TEST_PIN_CLIPBOARD\"\n", true) ||
      !writeFile(fakes.filePath(QStringLiteral("wl-paste")),
                 "#!/bin/sh\nexec " + cat + " \"$OMASNAP_TEST_PIN_CLIPBOARD\"\n", true)) {
    error = QStringLiteral("Could not isolate the notification commands");
    return false;
  }
  const EnvironmentGuard environment{
      "PATH", "OMASNAP_NOTIFY", "OMASNAP_RECENT_DIR", "OMASNAP_SCREENSHOT_DIR",
      "XDG_CONFIG_HOME", "OMASNAP_TEST_PIN_CLIPBOARD", "OMASNAP_TEST_COPY_FAIL"};
  qputenv("PATH", fakes.path().toUtf8());

  // A space keeps the path one argv word end to end.
  const QString image = root.filePath(QStringLiteral("shot one.png"));
  QImage pixels(1234, 56, QImage::Format_RGB32);
  pixels.fill(Qt::darkMagenta);
  if (!savePngFile(pixels, image, error))
    return false;

  return checkHelper(fakes, image, error) &&
         checkBinaryPropagation(fakes, image, error) &&
         checkRecentFallback(root, error) && runNotifyEditorSmoke(root.path(), error);
}
