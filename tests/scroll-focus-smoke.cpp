/** @fileoverview The scroll panel keeps the editor's keyboard focus while the
 *  page under its region is live, so Escape and Enter keep working. */
#include "scroll-focus-smoke.hpp"
#include "scroll-capture.hpp"

#include <LayerShellQt/Window>
#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QWidget>

bool runScrollFocusSmoke(QString &error) {
  QWidget host;
  host.resize(800, 600);
  static_cast<void>(host.winId());
  auto *layer = LayerShellQt::Window::get(host.windowHandle());
  if (!layer) {
    error = QStringLiteral("Could not create the scroll focus fixture");
    return false;
  }
  layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
  MonitorInfo monitor;
  monitor.geometry = QRect(0, 0, 800, 600);
  ScrollCapturePanel panel(monitor, layer, &host);
  bool dismissed = false;
  QObject::connect(&panel, &ScrollCapturePanel::dismissed, &panel,
                   [&dismissed] { dismissed = true; });
  panel.begin(QRect(100, 100, 400, 300));
  // Pointer motion over the live page and off the surface must not hand the
  // keyboard to the page.
  const QPointF inside(300, 250);
  QEnterEvent enter(inside, inside, inside);
  QApplication::sendEvent(&panel, &enter);
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&panel, &leave);
  if (layer->keyboardInteractivity() != LayerShellQt::Window::KeyboardInteractivityExclusive) {
    error = QStringLiteral("The live page took the scroll panel's keyboard focus");
    return false;
  }
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(&panel, &escape);
  panel.release();
  if (!dismissed ||
      layer->keyboardInteractivity() != LayerShellQt::Window::KeyboardInteractivityExclusive) {
    error = QStringLiteral("Escape did not cancel the live scroll region");
    return false;
  }
  return true;
}
