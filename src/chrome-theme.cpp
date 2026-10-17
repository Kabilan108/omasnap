#include "chrome-theme.hpp"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QLinearGradient>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <Qt>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {
QColor withAlpha(QColor color, qreal opacity) {
  color.setAlphaF(color.alphaF() * static_cast<float>(opacity));
  return color;
}

QString cssColor(const QColor &color) {
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}
} // namespace

const ChromeTheme &chromeTheme() {
  static const ChromeTheme theme;
  return theme;
}

QColor chromeAlpha(QColor color, int alpha) {
  return withAlpha(color, std::clamp(alpha, 0, 255) / 255.0);
}

void initializeChromeTheme() {
  qApp->setStyleSheet(chromeTheme().tooltipStyleSheet() +
                      chromeTheme().fileDialogStyleSheet());
}

QBrush ChromeThemeBorder::brush(const QRectF &bounds) const {
  if (colors.isEmpty())
    return Qt::NoBrush;
  if (colors.size() == 1)
    return colors.constFirst();
  const qreal radians = qDegreesToRadians(angle);
  const QPointF direction(std::cos(radians), std::sin(radians));
  const qreal halfLength = (std::abs(bounds.width() * direction.x()) +
                            std::abs(bounds.height() * direction.y())) /
                           2.0;
  QLinearGradient gradient(bounds.center() - direction * halfLength,
                           bounds.center() + direction * halfLength);
  for (qsizetype index = 0; index < colors.size(); ++index)
    gradient.setColorAt(static_cast<qreal>(index) /
                            static_cast<qreal>(colors.size() - 1),
                        colors[index]);
  return gradient;
}

QString ChromeTheme::tooltipStyleSheet() const {
  return QStringLiteral(
             "QToolTip { color: %1; background-color: %2; "
             "border: 1px solid %3; border-radius: 4px; padding: 0; }"
             "QPlainTextEdit { selection-background-color: %4; "
             "selection-color: %5; }")
      .arg(cssColor(tooltipText), cssColor(tooltipBackground),
           cssColor(tooltipBorder), cssColor(accent), cssColor(accentText));
}

QString ChromeTheme::fileDialogStyleSheet() const {
  // Scope every rule to the chooser: editor text remains transparent and
  // annotation colors are independent of the desktop theme.
  return QStringLiteral(
             "QFileDialog, QFileDialog QWidget { background: %1; color: %2; }"
             "QFileDialog QLineEdit, QFileDialog QAbstractItemView { "
             "background: %3; selection-background-color: %4; "
             "selection-color: %5; }"
             "QFileDialog QPushButton, QFileDialog QToolButton, "
             "QFileDialog QComboBox { background: %6; color: %7; "
             "border: 1px solid %8; border-radius: 4px; padding: 5px; }"
             "QFileDialog QPushButton:hover, QFileDialog QToolButton:hover { "
             "background: %9; color: %10; }"
             "QFileDialog QPushButton:default, QFileDialog QLineEdit:focus { "
             "border: 1px solid %4; }"
             "QFileDialog QWidget:disabled { color: %11; }")
      .arg(cssColor(surface), cssColor(foreground), cssColor(canvasSurface),
           cssColor(accent), cssColor(accentText), cssColor(button),
           cssColor(buttonText), cssColor(tooltipBorder),
           cssColor(buttonHover), cssColor(buttonHoverText), cssColor(muted));
}
