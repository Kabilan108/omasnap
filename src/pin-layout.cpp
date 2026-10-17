/** @fileoverview Implements pinned-surface layout helpers. */
#include "pin-layout.hpp"

#include <QImage>
#include <QRect>
#include <Qt>
#include <QtNumeric>
#include <QtTypes>
#include <QtMath>

#include <algorithm>
#include <cmath>

QSize pinFrameSize(const QSize &screenSize) {
  constexpr int width = 200;
  const double aspect =
      screenSize.width() > 0 && screenSize.height() > 0
          ? static_cast<double>(screenSize.height()) / screenSize.width()
          : 9.0 / 16.0;
  const int height = std::clamp(static_cast<int>(std::lround(width * aspect)),
                                width / 4, width * 2);
  return {width, height};
}

QImage pinDisplayImage(const QImage &image) {
  if (image.isNull())
    return {};
  // Cards cover their frame, centered horizontally and aligned at the top.
  // Nothing beyond these extremes can appear in the 200x50..400 frame.
  // Crop that invisible area first so a long scroll keeps sharp top pixels.
  const int width = static_cast<int>(std::min(qint64(image.width()),
                                             qint64(image.height()) * 4));
  const int height = static_cast<int>(std::min(qint64(image.height()),
                                              qint64(image.width()) * 2));
  const QRect crop((image.width() - width) / 2, 0, width, height);
  const QImage visible = crop == image.rect() ? image : image.copy(crop);
  // Four device pixels per logical pixel, without upscaling small captures.
  return width <= 800 && height <= 1600
             ? visible
             : visible.scaled(800, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QRectF pinControlRect(const QSize &frame, int index) {
  if (frame.isEmpty())
    return {};
  // Very wide displays produce previews only 50 pixels tall. Keep two
  // separate rows there so the centered actions never cover a corner control.
  const bool compact = frame.height() < 76;
  const qreal size = compact ? 19 : 23;
  const qreal inset = compact ? 4 : 7;
  const qreal gap = compact ? 3 : 6;
  constexpr qreal dragWidth = 18;
  constexpr qreal actionWidth = 52;
  constexpr qreal actionGap = 6;
  const qreal actionHeight = compact ? 20 : 26;
  const qreal actionY = std::max((frame.height() - actionHeight) / 2,
                                 inset + size + gap);
  const qreal right = frame.width() - inset - size;
  switch (index) {
  case 0: // Close
    return {right, inset, size, size};
  case 1: // Copy
    return {(frame.width() + actionGap) / 2, actionY, actionWidth, actionHeight};
  case 2: // Copy path
    return {inset + dragWidth + gap, inset, size, size};
  case 3: // Edit
    return {(frame.width() - actionGap) / 2 - actionWidth, actionY,
            actionWidth, actionHeight};
  case 4: // Drag out
    return {inset, inset, dragWidth, size};
  case 5: // Pin
    return {right - size - gap, inset, size, size};
  case 6: // Show in folder, beside Copy path
    return {inset + dragWidth + size + gap * 2, inset, size, size};
  default:
    return {};
  }
}

QString pinControlTip(int index, bool kept) {
  switch (index) {
  case 0:
    return QStringLiteral("Close · X / Esc / middle-click");
  case 2:
    return QStringLiteral("Copy saved file path · L / F");
  case 6:
    return QStringLiteral("Show in folder · R");
  case 4:
    return QStringLiteral("Drag this image out");
  case 5:
    return kept ? QStringLiteral("Unpin · T / Ctrl+P · fade after 10 seconds")
                 : QStringLiteral("Keep on screen · T / Ctrl+P");
  default:
    return {};
  }
}

int pinOutputIndex(const QString &requestedOutput,
                   const QStringList &availableOutputs) {
  if (requestedOutput.isEmpty())
    return -1;
  return availableOutputs.indexOf(requestedOutput);
}

QPoint pinSlotPosition(const QSize &screenSize, const QSize &pinSize,
                       const QSize &slotSize, int index, int gap, int margin) {
  const int verticalSpace = std::max(1, screenSize.height() - 2 * margin);
  const int rowHeight = std::max(1, slotSize.height() + gap);
  const int rows = std::max(1, (verticalSpace + gap) / rowHeight);
  const int row = std::max(0, index) % rows;
  const int column = std::max(0, index) / rows;
  const int x = screenSize.width() - margin - pinSize.width() -
                column * (std::max(1, slotSize.width()) + gap);
  const int y =
      screenSize.height() - margin - pinSize.height() - row * rowHeight;
  return {x, y};
}

QRect clampPinGeometry(const QRect &pin, const QRect &bounds) {
  if (bounds.isEmpty())
    return pin;
  const int x = std::clamp(pin.x(), bounds.left(),
                           std::max(bounds.left(), bounds.right() - pin.width() + 1));
  const int y = std::clamp(
      pin.y(), bounds.top(),
      std::max(bounds.top(), bounds.bottom() - pin.height() + 1));
  return {x, y, pin.width(), pin.height()};
}
