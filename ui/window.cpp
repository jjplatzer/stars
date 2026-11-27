#include "window.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <stdexcept>

#include "pane.h"
#include "scrollbar.h"
#include "select.h"
#include "theme.h"

namespace stars::ui {
namespace {

class ProfileNameEditor final : public QLineEdit {
 public:
  using QLineEdit::QLineEdit;
  std::function<void()> cancel;

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape) {
      event->accept();
      if (cancel) cancel();
      return;
    }
    QLineEdit::keyPressEvent(event);
  }
};

class ProfileAction final : public QPushButton {
 public:
  ProfileAction(QChar symbol, const QColor& hoverColor, QWidget* parent)
      : QPushButton(parent), symbol_(symbol), hoverColor_(hoverColor) {
    setFixedSize(30, 32);
    setCursor(Qt::PointingHandCursor);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setFont(materialSymbolsFont());
    painter.setPen(underMouse() ? hoverColor_ : QColor("#A9A9C0"));
    painter.drawText(rect(), Qt::AlignCenter, QString(symbol_));
    if (hasFocus()) {
      painter.setPen(QColor("#A9A9C0"));
      painter.drawRect(rect().adjusted(1, 1, -2, -2));
    }
  }
  void enterEvent(QEnterEvent* event) override {
    QPushButton::enterEvent(event);
    update();
  }
  void leaveEvent(QEvent* event) override {
    QPushButton::leaveEvent(event);
    update();
  }

 private:
  QChar symbol_;
  QColor hoverColor_;
};

QString positionCode(const Profile& profile, const ProfileCatalog& catalog) {
  if (!profile.tcp.isEmpty()) return profile.tcp;
  for (const auto& position : catalog.controllerPositions()) {
    if (position.adaptation + "/" + position.id == profile.position)
      return position.tcp;
  }
  return QStringLiteral("Unknown Position");
}

void addProfile(QListWidget* list, const Profile& profile,
                const ProfileCatalog& catalog,
                const std::function<void(const QString&)>& rename,
                const std::function<void()>& remove) {
  auto* item = new QListWidgetItem(list);
  item->setSizeHint(QSize(0, 66));
  item->setData(Qt::AccessibleTextRole,
                profile.name + QStringLiteral(", ") + profile.facility);
  item->setData(Qt::UserRole, profile.id);
  item->setToolTip(profile.name);

  auto* row = new QFrame;
  row->setObjectName("profileRow");
  row->setAttribute(Qt::WA_Hover);
  auto* rowLayout = new QHBoxLayout(row);
  rowLayout->setContentsMargins(14, 8, 8, 8);
  rowLayout->setSpacing(4);
  auto* text = new QWidget(row);
  text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* layout = new QVBoxLayout(text);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(3);
  rowLayout->addWidget(text, 1, Qt::AlignVCenter);

  auto* title = new QLabel(profile.name, row);
  title->setObjectName(QStringLiteral("profileName"));
  auto* detail = new QLabel(
      QStringLiteral("%1 @ %2/%3; %4")
          .arg(positionCode(profile, catalog), profile.artcc, profile.facility,
               profile.createdDate.isValid()
                   ? profile.createdDate.toString(Qt::ISODate)
                   : QStringLiteral("Unknown Date")),
      row);
  detail->setObjectName(QStringLiteral("profileDetail"));
  auto* namePages = new QStackedWidget(text);
  auto* editor = new ProfileNameEditor(profile.name, namePages);
  editor->setObjectName("inlineProfileName");
  editor->setAccessibleName("Profile Name");
  editor->setMaxLength(120);
  editor->setMinimumWidth(0);
  namePages->addWidget(title);
  namePages->addWidget(editor);
  // The hidden editor's minimum size must not stretch only the first line.
  // Center the two natural-height labels as one block inside the row.
  title->ensurePolished();
  detail->ensurePolished();
  namePages->setFixedHeight(title->sizeHint().height());
  detail->setFixedHeight(detail->sizeHint().height());
  layout->addWidget(namePages);
  layout->addWidget(detail);
  auto* renameButton = new ProfileAction(QChar(0xE3C9), QColor("#000000"), row);
  renameButton->setObjectName("renameProfile");
  renameButton->setAccessibleName("Rename Profile");
  renameButton->setToolTip("Rename Profile");
  auto* deleteButton = new ProfileAction(QChar(0xE872), QColor("#CD5C5C"), row);
  deleteButton->setObjectName("deleteProfile");
  deleteButton->setAccessibleName("Delete Profile");
  deleteButton->setToolTip("Delete Profile");
  rowLayout->addWidget(renameButton);
  rowLayout->addWidget(deleteButton);
  QObject::connect(renameButton, &QPushButton::clicked, editor,
                   [editor, title, namePages] {
                     editor->setText(title->text());
                     namePages->setFixedHeight(editor->sizeHint().height());
                     namePages->setCurrentWidget(editor);
                     editor->setFocus();
                     editor->selectAll();
                   });
  QObject::connect(editor, &QLineEdit::returnPressed, list, [editor, rename] {
    if (!editor->text().trimmed().isEmpty()) rename(editor->text());
  });
  editor->cancel = [editor, title, namePages, renameButton] {
    editor->setText(title->text());
    namePages->setCurrentWidget(title);
    namePages->setFixedHeight(title->sizeHint().height());
    renameButton->setFocus();
  };
  QObject::connect(deleteButton, &QPushButton::clicked, list, remove);
  list->setItemWidget(item, row);
}

}  // namespace

void ProfileWindow::refreshProfiles(QListWidget* list) {
  const auto profiles = store_.load();
  list->clear();
  for (const auto& profile : profiles) {
    addProfile(
        list, profile, catalog_,
        [this, list, profile](const QString& name) {
          try {
            store_.rename(profile.id, name);
            refreshProfiles(list);
          } catch (const std::exception& error) {
            QMessageBox::critical(this, tr("Could Not Rename Profile"),
                                  QString::fromUtf8(error.what()));
          }
        },
        [this, list, profile] {
          QMessageBox dialog(QMessageBox::NoIcon, tr("Delete Profile"),
                             tr("Delete \"%1\"?").arg(profile.name),
                             QMessageBox::NoButton, this);
          dialog.setTextFormat(Qt::PlainText);
          auto* cancel =
              dialog.addButton(tr("Cancel"), QMessageBox::RejectRole);
          auto* remove =
              dialog.addButton(tr("Delete"), QMessageBox::DestructiveRole);
          remove->setObjectName("confirmDeleteProfile");
          dialog.setDefaultButton(cancel);
          dialog.setEscapeButton(cancel);
          dialog.exec();
          if (dialog.clickedButton() != remove) return;
          try {
            store_.remove(profile.id);
            refreshProfiles(list);
          } catch (const std::exception& error) {
            QMessageBox::critical(this, tr("Could Not Delete Profile"),
                                  QString::fromUtf8(error.what()));
          }
        });
  }
}

ProfileWindow::ProfileWindow(QWidget* parent, const QString& profilesPath)
    : QWidget(parent), catalog_(ProfileCatalog::load()), store_(profilesPath) {
  setWindowTitle(QStringLiteral("STARS"));
  setObjectName(QStringLiteral("profileWindow"));
  // CRC ProfileListDialog: Height=500, MinWidth=350, SizeToContent=Width.
  resize(350, 500);
  setMinimumSize(350, 340);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  auto* pages = new QStackedWidget(this);
  pages->setObjectName("profilePages");
  root->addWidget(pages);
  auto* home = new QWidget(pages);
  pages->addWidget(home);
  auto* layout = new QVBoxLayout(home);
  layout->setContentsMargins(16, 18, 16, 16);
  layout->setSpacing(12);

  auto* profiles = new QListWidget(this);
  installOverlayScrollbar(profiles);
  profiles->setObjectName(QStringLiteral("profiles"));
  profiles->setAccessibleName(QStringLiteral("Profiles"));
  profiles->setFrameShape(QFrame::NoFrame);
  profiles->setSelectionMode(QAbstractItemView::SingleSelection);
  profiles->setEditTriggers(QAbstractItemView::NoEditTriggers);
  profiles->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  profiles->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  profiles->setSpacing(3);
  profiles->setMouseTracking(true);

  connect(
      profiles, &QListWidget::itemDoubleClicked, this,
      [this, pages, home, profiles](QListWidgetItem* item) {
        try {
          for (const auto& profile : store_.load())
            if (profile.id == item->data(Qt::UserRole).toString()) {
              for (auto* pane : findChildren<Pane*>())
                if (pane->objectName() == profile.id) {
                  pane->raise();
                  return;
                }
              const auto tcp = positionCode(profile, catalog_);
              auto* pane =
                  new Pane(profile.name, this, profile.position, tcp,
                           QStringLiteral(" - %1 @ %2/%3")
                               .arg(tcp, profile.artcc, profile.facility));
              connect(pane, &Pane::switchProfileRequested, this,
                      [this, pages, home, profiles] {
                        pages->setCurrentWidget(home);
                        setWindowState(windowState() & ~Qt::WindowMinimized);
                        show();
                        raise();
                        activateWindow();
                        profiles->setFocus();
                      });
              pane->configurePrefSets(
                  profile.prefSets,
                  [store = store_, id = profile.id](
                      const QJsonObject& previous, const QJsonObject& updated) {
                    store.setPrefSets(id, previous, updated);
                  });
              pane->configureSymbols(
                  profile.subsetSymbols,
                  [store = store_, id = profile.id](const QStringList& codes) {
                    store.setSubsetSymbols(id, codes);
                  });
              pane->configureStipple(
                  profile.faaStipple,
                  [store = store_, id = profile.id](bool enabled) {
                    store.setFaaStipple(id, enabled);
                  });
              pane->configureFont(profile.fontSetB,
                                  [store = store_, id = profile.id](bool setB) {
                                    store.setFontSetB(id, setB);
                                  });
              pane->setAdaptation(profile.colors,
                                  [store = store_, id = profile.id](
                                      const ColorOverrides& changes) {
                                    store.setColors(id, changes);
                                  });
              pane->setObjectName(profile.id);
              return;
            }
        } catch (const std::exception& error) {
          QMessageBox::critical(this, tr("Could Not Open STARS"),
                                QString::fromUtf8(error.what()));
        }
      });
  refreshProfiles(profiles);
  layout->addWidget(profiles, 1);

  auto* create = new QPushButton(QStringLiteral("Create New Profile"), this);
  create->setObjectName(QStringLiteral("createProfile"));
  create->setMinimumHeight(38);
  layout->addWidget(create);

  auto* formPage = new QWidget(pages);
  formPage->setObjectName("newProfilePage");
  pages->addWidget(formPage);
  auto* formLayout = new QVBoxLayout(formPage);
  formLayout->setContentsMargins(16, 18, 16, 16);
  formLayout->setSpacing(18);
  auto* scroll = new QScrollArea(formPage);
  installOverlayScrollbar(scroll);
  scroll->setObjectName("profileFormScroll");
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* form = new QWidget(scroll);
  form->setObjectName("profileForm");
  auto* fields = new QVBoxLayout(form);
  fields->setContentsMargins(0, 0, 0, 0);
  fields->setSpacing(0);
  const auto addField = [form, fields](const QString& label, const QString& id,
                                       const QString& placeholder) {
    auto* caption = new QLabel(label, form);
    caption->setObjectName("fieldLabel");
    auto* select = new SearchSelect(placeholder, form);
    select->setSearchEnabled(id == "positionSelect");
    select->setObjectName(id);
    select->setAccessibleName(label);
    caption->setBuddy(select);
    fields->addWidget(caption);
    fields->addSpacing(6);
    fields->addWidget(select);
    fields->addSpacing(18);
    return select;
  };
  auto* system = addField(tr("System"), "systemSelect", tr("Select System"));
  auto* artcc = addField(tr("ARTCC"), "artccSelect", tr("Select ARTCC"));
  auto* facility =
      addField(tr("Facility"), "facilitySelect", tr("Select Facility"));
  auto* position = addField(tr("Controller Position"), "positionSelect",
                            tr("Select Controller Position"));

  auto* nameLayout = new QVBoxLayout;
  nameLayout->setSpacing(6);
  auto* nameCaption = new QLabel(tr("Profile Name"), form);
  auto* name = new QLineEdit(form);
  nameCaption->setObjectName("fieldLabel");
  name->setObjectName("profileNameInput");
  name->setAccessibleName(tr("Profile Name"));
  name->setMinimumHeight(38);
  name->setMaxLength(120);
  nameCaption->setBuddy(name);
  nameLayout->addWidget(nameCaption);
  nameLayout->addWidget(name);
  fields->addLayout(nameLayout);
  fields->addStretch(1);
  scroll->setWidget(form);
  formLayout->addWidget(scroll, 1);

  auto* actions = new QHBoxLayout;
  actions->setSpacing(12);
  auto* back = new QPushButton(tr("Cancel"), formPage);
  back->setObjectName("backToProfiles");
  back->setMinimumHeight(38);
  actions->addWidget(back);
  auto* save = new QPushButton(tr("Create"), formPage);
  save->setObjectName("saveProfile");
  save->setMinimumHeight(38);
  save->setEnabled(false);
  actions->addWidget(save, 1);
  formLayout->addLayout(actions);

  const auto selectedSystem = [system] {
    return system->selectedId() == "TDW" ? System::Tdw : System::Tcw;
  };
  const auto updateFacilities = [this, selectedSystem, artcc, facility,
                                 position] {
    const QSignalBlocker block(facility);
    facility->setOptions(
        catalog_.facilities(selectedSystem(), artcc->selectedId()));
    position->setOptions({});
  };
  connect(system, &SearchSelect::selectionChanged, this,
          [this, selectedSystem, artcc, updateFacilities] {
            const auto previous = artcc->selectedId();
            const QSignalBlocker block(artcc);
            artcc->setOptions(catalog_.artccs(selectedSystem()));
            artcc->select(previous);
            updateFacilities();
          });
  connect(artcc, &SearchSelect::selectionChanged, this, updateFacilities);
  connect(facility, &SearchSelect::selectionChanged, this,
          [this, selectedSystem, artcc, facility, position] {
            position->setOptions(catalog_.positions(
                selectedSystem(), artcc->selectedId(), facility->selectedId()));
          });
  system->setOptions(
      {{"TCW", "TCW", "Terminal control approach departure", false},
       {"TDW", "TDW", "Tower display", false}});
  system->select("TCW");

  const auto updateCreate = [name, system, artcc, facility, position, save] {
    save->setEnabled(
        !name->text().trimmed().isEmpty() && !system->selectedId().isEmpty() &&
        !artcc->selectedId().isEmpty() && !facility->selectedId().isEmpty() &&
        !position->selectedId().isEmpty());
  };
  connect(name, &QLineEdit::textChanged, this, updateCreate);
  connect(position, &SearchSelect::selectionChanged, this, updateCreate);
  connect(
      save, &QPushButton::clicked, this,
      [this, pages, home, profiles, name, system, artcc, facility, position] {
        try {
          Profile profile{{},
                          name->text(),
                          system->selectedId(),
                          artcc->selectedId(),
                          facility->selectedId(),
                          position->selectedId()};
          profile.tcp = positionCode(profile, catalog_);
          store_.create(profile);
          refreshProfiles(profiles);
          profiles->setCurrentRow(profiles->count() - 1);
          pages->setCurrentWidget(home);
          profiles->setFocus(Qt::OtherFocusReason);
        } catch (const std::exception& error) {
          QMessageBox::critical(this, tr("Could not create profile"),
                                QString::fromUtf8(error.what()));
        }
      });
  connect(name, &QLineEdit::returnPressed, save, &QPushButton::click);

  connect(create, &QPushButton::clicked, this,
          [this, pages, formPage, system, artcc, updateFacilities, name] {
            name->clear();
            system->select("TCW");
            artcc->setOptions(catalog_.artccs(System::Tcw));
            updateFacilities();
            pages->setCurrentWidget(formPage);
            system->setFocus(Qt::OtherFocusReason);
          });
  connect(back, &QPushButton::clicked, this, [pages, home, create] {
    pages->setCurrentWidget(home);
    create->setFocus(Qt::OtherFocusReason);
  });
}

}  // namespace stars::ui
