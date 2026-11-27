#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QPixmap>
#include <QTimer>
#include <stdexcept>

#include "theme.h"
#include "window.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("STARS"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("STARS radar display"));
  parser.addHelpOption();
  // Used to inspect the actual Qt layout without screen-capture permissions.
  QCommandLineOption screenshot(
      QStringLiteral("screenshot"),
      QStringLiteral("Save a layout preview and exit"), QStringLiteral("path"));
  parser.addOption(screenshot);
  parser.process(app);

  try {
    stars::ui::applyTheme(app);

    stars::ui::ProfileWindow window;
    window.show();
    if (parser.isSet(screenshot)) {
      QTimer::singleShot(150, &window, [&] {
        app.exit(window.grab().save(parser.value(screenshot)) ? 0 : 1);
      });
    }
    return app.exec();
  } catch (const std::exception& error) {
    qCritical() << error.what();
    return 1;
  }
}
