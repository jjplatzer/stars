#pragma once

#include <QWidget>

#include "catalog.h"
#include "store.h"

class QListWidget;

namespace stars::ui {

class ProfileWindow final : public QWidget {
 public:
  explicit ProfileWindow(QWidget* parent = nullptr,
                         const QString& profilesPath = {});

 private:
  void refreshProfiles(QListWidget* list);
  ProfileCatalog catalog_;
  ProfileStore store_;
};

}  // namespace stars::ui
