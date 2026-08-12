#include "image-input.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir root;
  if (!root.isValid())
    return 1;

  const QString validPath =
      QDir(root.path()).filePath(QStringLiteral("image with spaces.png"));
  QImage image(32, 24, QImage::Format_RGB32);
  image.fill(Qt::cyan);
  if (!image.save(validPath, "PNG"))
    return 2;

  QString resolved;
  QString error;
  if (!resolveImagePath(validPath, resolved, error) ||
      resolved != QFileInfo(validPath).canonicalFilePath())
    return 3;
  resolved.clear();
  error.clear();
  if (!resolveImagePath(QUrl::fromLocalFile(validPath).toString(), resolved,
                        error) ||
      resolved != QFileInfo(validPath).canonicalFilePath())
    return 4;

  resolved.clear();
  error.clear();
  if (resolveImagePath(
          QDir(root.path()).filePath(QStringLiteral("missing.png")), resolved,
          error) ||
      error.isEmpty())
    return 5;

  const QString invalidPath =
      QDir(root.path()).filePath(QStringLiteral("invalid image.png"));
  QFile invalid(invalidPath);
  if (!invalid.open(QIODevice::WriteOnly) ||
      invalid.write("not an image\n") <= 0)
    return 6;
  invalid.close();
  resolved.clear();
  error.clear();
  if (resolveImagePath(invalidPath, resolved, error) || error.isEmpty())
    return 7;

  resolved.clear();
  error.clear();
  if (resolveImagePath(root.path(), resolved, error) || error.isEmpty())
    return 8;
  return 0;
}
