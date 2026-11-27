#pragma once

#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

#include "palette.h"

namespace stars::ui {

struct Profile {
  QString id;
  QString name;
  QString system;
  QString artcc;
  QString facility;
  // CRC adaptation ID / position ID, as returned by ProfileCatalog.
  QString position;
  QDate createdDate;
  QString tcp;
  bool fontSetB = true;
  ColorOverrides colors;
  bool faaStipple = false;
  QJsonObject prefSets;
  QStringList subsetSymbols;
};

class ProfileStore {
 public:
  explicit ProfileStore(const QString& path = {});
  QList<Profile> load() const;
  QList<Profile> create(Profile profile) const;
  QList<Profile> rename(const QString& id, const QString& name) const;
  void setPrefSets(const QString& id, const QJsonObject& previous,
                   const QJsonObject& updated) const;
  void setSubsetSymbols(const QString& id, const QStringList& codes) const;
  void setFaaStipple(const QString& id, bool enabled) const;
  void setFontSetB(const QString& id, bool setB) const;
  void setColors(const QString& id, const ColorOverrides& changes) const;
  QList<Profile> remove(const QString& id) const;

 private:
  QList<Profile> update(
      const std::function<void(QList<Profile>&)>& mutate) const;
  QString path_;
};

}  // namespace stars::ui
