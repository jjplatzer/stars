#pragma once

#include <QPushButton>

#include "option.h"

class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;

namespace stars::ui {

// A bounded list popup with optional search. Only explicit selection commits a
// value.
class SearchSelect final : public QPushButton {
  Q_OBJECT
 public:
  explicit SearchSelect(const QString& placeholder, QWidget* parent = nullptr);
  void setSearchEnabled(bool enabled);
  void setOptions(const QList<SelectionOption>& options);
  void select(const QString& id);
  QString selectedId() const { return selectedId_; }
  QList<SelectionOption> options() const { return options_; }

 signals:
  void selectionChanged();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  void openPopup();
  void resizePopup();
  void filter(const QString& query);
  void chooseCurrent();

  QString selectedId_;
  QList<SelectionOption> options_;
  QFrame* popup_;
  QLineEdit* search_;
  QListWidget* list_;
  QLabel* empty_;
  bool searchEnabled_ = true;
};

}  // namespace stars::ui
