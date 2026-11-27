#include "stars.h"

namespace stars {
void STARSPane::configureAlertSite() {
  // All pane input and drawing run on the UI thread. Supervisor switches
  // survive switching profiles within the same facility, as in REDS.
  static QMap<QString, std::shared_ptr<AlertSite>> sites;
  if (facility_.isEmpty()) return;
  auto& site = sites[artcc_ + '/' + facility_];
  if (!site) site = std::make_shared<AlertSite>();
  // A facility's first open pane starts a fresh traffic session.
  if (site.use_count() == 1) {
    site->acknowledged.clear();
    site->lowAltitude.clear();
  }
  alertSite_ = site;
}

bool STARSPane::alertEnabled(const QString& function) const {
  const auto& off = alertSite_->inhibited;
  if (function == "MCI") return !off.contains("CA") && !off.contains("MCI");
  if (function == "APP") return !off.contains("MSAW") && !off.contains("APP");
  return (function == "CA" || function == "MSAW") && !off.contains(function);
}

bool STARSPane::supervisorCommand() {
  QString function;
  if (mode_ == Mode::CA) {
    if (input_ == "AE" || input_ == "AI")
      function = "CA";
    else if (input_ == "ME" || input_ == "MI")
      function = "MCI";
    else {
      response_ = "FORMAT";
      return true;
    }
  } else if (mode_ == Mode::Multi) {
    if (input_ == "VME" || input_ == "VMI")
      function = "MSAW";
    else if (input_ == "VGE" || input_ == "VGI")
      function = "APP";
    else if (input_.startsWith("2ATPA") || input_.startsWith("2.5")) {
      response_ = atpa_.supervisor(input_);
      if (!response_.isEmpty() && response_ != "NO CHANGE") return true;
      atpa_.update(traffic_.tracks(), false);
    } else
      return false;
  } else
    return false;

  if (!function.isEmpty()) {
    if (function == "APP" && !alertEnabled("MSAW")) {
      response_ = "ILL FNCT - MSAW OFF";
      return true;
    }
    auto& off = alertSite_->inhibited;
    const bool inhibit = input_.endsWith('I');
    response_.clear();
    if (off.contains(function) == inhibit)
      response_ = "NO CHANGE";
    else if (inhibit)
      off.insert(function);
    else
      off.remove(function);
  }
  mode_ = Mode::None;
  input_.clear();
  return true;
}
}  // namespace stars
