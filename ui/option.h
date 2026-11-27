#pragma once

#include <QString>

namespace stars::ui {

struct SelectionOption {
  QString id;
  QString label;
  QString searchText;
  bool starred = false;
};

}  // namespace stars::ui
