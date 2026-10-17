/** @fileoverview Implements pinned snapshot and slot locking. */
#include "pin-file.hpp"

#include "capture.hpp"
#include "recent-snaps.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QtLogging>
#include <memory>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

PinSnapshotFile::PinSnapshotFile(QString path)
    : path_(QFileInfo(path).absoluteFilePath()) {
  fd_ = ::open(QFile::encodeName(path_).constData(),
               O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd_ < 0 || ::flock(fd_, LOCK_SH | LOCK_NB) != 0) {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = -1;
  }
}

PinSnapshotFile::~PinSnapshotFile() {
  if (fd_ < 0)
    return;
  // Only the last holder may unlink. Upgrade SH -> EX; if another pin still
  // holds SH, leave the file for that process.
  const bool lastOwner = !preserve_ && ::flock(fd_, LOCK_EX | LOCK_NB) == 0;
  if (lastOwner && isOwnedPath(path_)) {
    QFile::remove(path_);
    QFile::remove(operationLogPath(path_));
    QFile::remove(thumbnailPath(path_));
    QFile::remove(previewPath());
    QFile::remove(operationLogPath(previewPath()));
  }
  ::close(fd_);
}

bool PinSnapshotFile::isLocked() const { return fd_ >= 0; }

bool PinSnapshotFile::isOwnedPath(const QString &path) {
  static const QRegularExpression internalName(
      QStringLiteral("^pin-[1-9][0-9]*-[1-9][0-9]*-[0-9a-f]{16}\\.png$"));
  const QFileInfo file(path);
  const QString runtime = secureRuntimeDirectory();
  return !runtime.isEmpty() && file.absolutePath() == runtime &&
         internalName.match(file.fileName()).hasMatch() && !file.isSymLink();
}

void PinSnapshotFile::preserveForEditor() { preserve_ = true; }

void PinSnapshotFile::finishSavedPreview(const OperationLog &log,
                                        const QString &savedPath) {
  OperationLog completed = log;
  completed.savedPath = savedPath;
  QString error;
  if (!saveOperationLog(operationLogPath(path_), completed, error))
    qWarning("Could not replace the originating preview: %s", qPrintable(error));
}

PinSlotLock::PinSlotLock() {
  const QString runtime = secureRuntimeDirectory();
  if (runtime.isEmpty())
    return;
  for (int candidate = 0; candidate < 1024; ++candidate) {
    const QString path = QDir(runtime).filePath(
        QStringLiteral(".pin-slot-%1.lock").arg(candidate));
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                          S_IRUSR | S_IWUSR);
    if (fd < 0)
      continue;
    if (::fchmod(fd, S_IRUSR | S_IWUSR) == 0 &&
        ::flock(fd, LOCK_EX | LOCK_NB) == 0) {
      fd_ = fd;
      index_ = candidate;
      return;
    }
    ::close(fd);
  }
}

PinSlotLock::~PinSlotLock() {
  if (fd_ < 0)
    return;
  ::close(fd_);
}

bool PinSlotLock::isLocked() const { return fd_ >= 0; }

int PinSlotLock::index() const { return index_; }

std::shared_ptr<PinSnapshotFile> copyPinDocument(const QString &path, QString &error) {
  OperationLog log;
  const QString sidecar = operationLogPath(path);
  if (QFile::exists(sidecar) && !loadOperationLog(sidecar, log, error))
    return {};
  // The visible PNG is flattened. If its capture is still on the shelf,
  // restore the working document so editing a preview retains its layers too.
  QString source = path;
  QString recentError;
  if (const auto recent = findRecentSnap(log.recentId, &recentError)) {
    if (!loadOperationLog(recent->logPath, log, error))
      return {};
    source = recent->sourcePath;
  }
  if (!recentError.isEmpty()) {
    error = recentError;
    return {};
  }
  // This is a new working document, not a completed export. Further editing
  // must not keep pointing Reveal at the preceding saved version.
  log.savedPath.clear();
  const QString copy = pinnedSnapshotPath(1);
  if (copy.isEmpty() || !QFile::copy(source, copy)) {
    error = QStringLiteral("Could not retain the pinned capture for editing");
    return {};
  }
  auto document = std::make_shared<PinSnapshotFile>(copy);
  if (!document->isLocked()) {
    QFile::remove(copy);
    error = QStringLiteral("Could not lock the pinned document");
    return {};
  }
  if (!QFile::setPermissions(copy, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error = QStringLiteral("Could not make the pinned document private");
    return {};
  }
  if (!saveOperationLog(operationLogPath(copy), log, error))
    return {};
  return document;
}
