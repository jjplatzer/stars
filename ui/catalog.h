#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>

#include "option.h"

namespace stars::ui {

enum class System { Tcw, Tdw };

struct ControllerPosition {
  QString id;
  QString artcc;
  QString facility;
  // A tower can use its parent TRACON's adaptation. Keep both identifiers.
  QString adaptation;
  QString tcp;
  QString name;
  QString callsign;
  System system;
  bool starred;
};

class ProfileCatalog {
 public:
  static ProfileCatalog load();
  ProfileCatalog(const QJsonObject& metadata,
                 const QList<QJsonObject>& configurations);

  QList<SelectionOption> artccs(System system) const;
  QList<SelectionOption> facilities(System system, const QString& artcc) const;
  QList<SelectionOption> positions(System system, const QString& artcc,
                                   const QString& facility) const;
  const QList<ControllerPosition>& controllerPositions() const {
    return positions_;
  }

 private:
  QString facilityName(const QString& id, const QStringList& preferredTypes,
                       const QString& fallback) const;
  QMap<QString, QMap<QString, QString>> names_;
  QMap<QString, QString> configurationNames_;
  QList<ControllerPosition> positions_;
};

}  // namespace stars::ui
