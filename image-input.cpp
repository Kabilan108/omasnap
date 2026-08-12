#include "image-input.hpp"

#include <QFileInfo>
#include <QImageReader>
#include <QUrl>

bool resolveImagePath(const QString &input, QString &resolved, QString &error) {
  const QUrl url(input);
  const QString candidate = url.isLocalFile() ? url.toLocalFile() : input;
  const QFileInfo info(candidate);
  if (!info.exists()) {
    error = QStringLiteral("Image file does not exist: %1").arg(candidate);
    return false;
  }
  if (!info.isFile()) {
    error =
        QStringLiteral("Image path is not a regular file: %1").arg(candidate);
    return false;
  }
  if (!info.isReadable()) {
    error = QStringLiteral("Image file is not readable: %1").arg(candidate);
    return false;
  }
  resolved = info.canonicalFilePath();
  if (resolved.isEmpty()) {
    error = QStringLiteral("Could not resolve image path: %1").arg(candidate);
    return false;
  }
  QImageReader reader(resolved);
  if (!reader.canRead()) {
    error = QStringLiteral("Image data could not be decoded: %1").arg(resolved);
    return false;
  }
  return true;
}
