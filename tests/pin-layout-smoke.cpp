/** @fileoverview Tests pinned-surface layout helpers. */
#include "pin-layout-smoke.hpp"

#include "pin-layout.hpp"
#include "cli-path.hpp"
#include <QCommandLineParser>
#include <QImage>
#include <QRectF>
#include <QPainter>
#include <QtTypes>
#include <Qt>

#include <QSet>

bool runPinLayoutSmoke(QString &error) {
  // Keep the entire area any card can show. Long scrolls must not turn into
  // a one-pixel-wide thumbnail, and ultrawides keep their centered crop.
  QImage tallImage(80, 2000, QImage::Format_RGB32);
  tallImage.fill(Qt::red);
  QImage wideImage(2000, 80, QImage::Format_RGB32);
  wideImage.fill(Qt::red);
  {
    QPainter painter(&tallImage);
    painter.fillRect(0, 0, 80, 160, Qt::green);
  }
  {
    QPainter painter(&wideImage);
    painter.fillRect(840, 0, 320, 80, Qt::green);
  }
  QImage tallExpected(80, 160, QImage::Format_RGB32);
  tallExpected.fill(Qt::green);
  QImage wideExpected(320, 80, QImage::Format_RGB32);
  wideExpected.fill(Qt::green);
  QImage monitorImage(1920, 1080, QImage::Format_RGB32);
  monitorImage.fill(Qt::blue);
  if (pinDisplayImage(tallImage) != tallExpected ||
      pinDisplayImage(wideImage) != wideExpected ||
      pinDisplayImage(monitorImage).size() != QSize(800, 450) ||
      !pinDisplayImage({}).isNull()) {
    error = QStringLiteral("Pin thumbnails changed the visible crop or lost their size bound");
    return false;
  }

  // The frame follows the display's shape at a fixed width, clamps the
  // extremes, and guesses 16:9 when the display cannot be asked.
  if (pinFrameSize(QSize(2560, 1600)) != QSize(200, 125) ||
      pinFrameSize(QSize(3440, 1440)) != QSize(200, 84) ||
      pinFrameSize(QSize(1080, 1920)) != QSize(200, 356) ||
      pinFrameSize(QSize(1000, 5000)) != QSize(200, 400) ||
      pinFrameSize(QSize(5000, 500)) != QSize(200, 50) ||
      pinFrameSize(QSize()) != QSize(200, 113)) {
    error = QStringLiteral("Pin frames did not follow the display's shape");
    return false;
  }

  const QSize preview(200, 113);

  // Controls remain distinct click targets even on a 50-pixel-tall preview.
  for (const int height : {50, 75, 76, 113, 356, 400}) {
    const QSize frame(200, height);
    const QRectF bounds{QPointF(), QSizeF(frame)};
    for (int control = 0; control < 7; ++control) {
      const QRectF rect = pinControlRect(frame, control);
      if (rect.isEmpty() || !bounds.contains(rect)) {
        error = QStringLiteral("A pin control fell outside the preview");
        return false;
      }
      for (int other = control + 1; other < 7; ++other) {
        if (rect.intersects(pinControlRect(frame, other))) {
          error = QStringLiteral("Pin controls overlap on a short preview");
          return false;
        }
      }
    }
    const QRectF edit = pinControlRect(frame, 3);
    const QRectF copy = pinControlRect(frame, 1);
    const QRectF pin = pinControlRect(frame, 5);
    const QRectF close = pinControlRect(frame, 0);
    const QRectF path = pinControlRect(frame, 2);
    const QRectF folder = pinControlRect(frame, 6);
    if (edit.right() >= copy.left() ||
        edit.united(copy).center().x() != frame.width() / 2.0 ||
        (height >= 113 && edit.center().y() != height / 2.0) ||
        pin.right() >= close.left() || pin.top() != close.top() ||
        path.right() >= folder.left() || path.top() != folder.top()) {
      error = QStringLiteral("Pin controls lost their centered actions or paired icons");
      return false;
    }
  }
  if (!pinControlRect(preview, -1).isEmpty() ||
      !pinControlRect(preview, 7).isEmpty()) {
    error = QStringLiteral("An unknown pin control has a click target");
    return false;
  }

  // Edit and Copy explain themselves with labels; only icons need hover tips.
  QSet<QString> tips;
  for (const int control : {0, 2, 4, 5, 6}) {
    if (pinControlTip(control).isEmpty()) {
      error = QStringLiteral("A pin control has no tooltip");
      return false;
    }
    tips.insert(pinControlTip(control));
  }
  if (tips.size() != 5 || !pinControlTip(1).isEmpty() ||
      !pinControlTip(3).isEmpty() || !pinControlTip(7).isEmpty() ||
      !pinControlTip(-1).isEmpty() || pinControlTip(5, true) == pinControlTip(5, false)) {
    error = QStringLiteral("Pin control tooltips repeat or overflow");
    return false;
  }

  const QList<QPair<QStringList, bool>> invocations{
      {{QStringLiteral("--pin"), QStringLiteral("image.png")}, true},
      {{QStringLiteral("--pin=image.png")}, true},
      {{QStringLiteral("--preview=image.png")}, true},
      {{QStringLiteral("-platform"), QStringLiteral("offscreen"),
         QStringLiteral("--preview"), QStringLiteral("image.png")}, true},
      {{QStringLiteral("-platform"), QStringLiteral("offscreen"),
         QStringLiteral("--pin=image.png")}, true},
      {{QStringLiteral("--pin"), QStringLiteral("image.png"),
         QStringLiteral("-platformtheme"), QStringLiteral("gtk3")}, true},
      {{QStringLiteral("--"), QStringLiteral("--pin")}, false},
      {{QStringLiteral("--"), QStringLiteral("--pin=image.png")}, false},
      {{QStringLiteral("--"), QStringLiteral("--preview=image.png")}, false},
      {{QStringLiteral("--file"), QStringLiteral("--pin")}, false}};
  for (const auto &[arguments, pinned] : invocations) {
    QCommandLineParser parser;
    configureCaptureCommandLine(parser, true);
    if (!parser.parse(QStringList{QStringLiteral("omasnap")} + arguments) ||
        (parser.isSet(QStringLiteral("pin")) || parser.isSet(QStringLiteral("preview"))) != pinned) {
      error = QStringLiteral("Pin argv selected the wrong Wayland shell");
      return false;
    }
  }

  const QSize slotScreen(400, 300);
  const QSize slotPin(100, 80);
  if (pinSlotPosition(slotScreen, slotPin, slotPin, 0, 10, 14) != QPoint(286, 206) ||
      pinSlotPosition(slotScreen, slotPin, slotPin, 1, 10, 14) != QPoint(286, 116) ||
      pinSlotPosition(slotScreen, slotPin, slotPin, 2, 10, 14) != QPoint(286, 26) ||
      pinSlotPosition(slotScreen, slotPin, slotPin, 3, 10, 14) != QPoint(176, 206)) {
    error = QStringLiteral("Pinned slots did not stack and wrap correctly");
    return false;
  }

  const QRect bounds(QPoint(10, 20), QSize(400, 300));
  if (clampPinGeometry(QRect(380, 290, 100, 80), bounds) != QRect(310, 240, 100, 80) ||
      clampPinGeometry(QRect(-20, 0, 100, 80), bounds) != QRect(10, 20, 100, 80)) {
    error = QStringLiteral("Pinned geometry was not clamped to the screen");
    return false;
  }

  const QStringList outputs{QStringLiteral("eDP-1"), QStringLiteral("DP-4")};
  if (pinOutputIndex(QStringLiteral("DP-4"), outputs) != 1 ||
      pinOutputIndex(QStringLiteral("missing"), outputs) != -1 ||
      pinOutputIndex(QString(), outputs) != -1) {
    error = QStringLiteral("Pinned output selection did not use an exact name");
    return false;
  }
  return true;
}
