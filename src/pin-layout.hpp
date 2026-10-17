/** @fileoverview Provides pure pinned-surface layout helpers. */
#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

/// The frame a pin fills: the display's aspect at a fixed width, clamped
/// so a tall pivot or an ultrawide still yields a pin rather than a line;
/// 16:9 when the display cannot be asked.
[[nodiscard]] QSize pinFrameSize(const QSize &screenSize);

/// Bounded pixels for displaying any pin frame, including tall scroll captures.
/// Call on a worker; never use this image for output or editing.
[[nodiscard]] QImage pinDisplayImage(const QImage &image);

/// Control geometry shared by painting and hit testing: labeled actions in
/// the center, drag/path/folder at top-left, and pin/close at top-right.
[[nodiscard]] QRectF pinControlRect(const QSize &frame, int index);

/** Hover tips for icon controls; labeled actions and unknown controls are empty. */
[[nodiscard]] QString pinControlTip(int index, bool kept = false);

/// Bottom-right slot `index`, stacking upward and wrapping into columns.
[[nodiscard]] QPoint pinSlotPosition(const QSize &screenSize, const QSize &pinSize,
                                     const QSize &slotSize, int index, int gap,
                                     int margin);
[[nodiscard]] QRect clampPinGeometry(const QRect &pin, const QRect &bounds);
/// Index of the exactly named output, or -1 when it is empty or missing.
[[nodiscard]] int pinOutputIndex(const QString &requestedOutput,
                                 const QStringList &availableOutputs);
