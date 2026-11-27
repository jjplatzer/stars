#pragma once
#include <QFont>

class QApplication;
class QLineEdit;

namespace stars::ui {
void applyTheme(QApplication& app);
QFont interBlackFont();
QFont materialSymbolsFont();
void configureSearchField(QLineEdit* field, const QString& accessibleName);
}  // namespace stars::ui
