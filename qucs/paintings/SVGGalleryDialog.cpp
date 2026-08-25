/// @file SvgGalleryDialog.cpp
/// @brief Gallery for SVG symbols (class implementation)
/// @authors Andrés Martínez Mera - andresmmera@protonmail.com
/// @date Aug 6, 2026

#include "SVGGalleryDialog.h"
#include "main.h"
#include "qpainter.h"
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

SvgGalleryDialog::SvgGalleryDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("SVG Symbol Gallery"));
  resize(600, 400);

  auto* mainLayout = new QVBoxLayout(this);

  m_filterEdit = new QLineEdit(this);
  m_filterEdit->setPlaceholderText(tr("Search symbols..."));
  mainLayout->addWidget(m_filterEdit);

  // --- Library path row -------------------------------------------
  auto* libraryLayout = new QHBoxLayout;
  m_libraryPathLabel = new QLabel(this);
  m_libraryPathLabel->setStyleSheet("color: gray; font-style: italic;");
  m_libraryPathButton = new QPushButton(tr("Set Library Path..."), this);
  libraryLayout->addWidget(m_libraryPathLabel, /*stretch=*/1);
  libraryLayout->addWidget(m_libraryPathButton);
  mainLayout->addLayout(libraryLayout);
  // ------------------------------------------------------------------

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  m_categoryList = new QListWidget(splitter);
  m_categoryList->setMaximumWidth(160);

  m_itemList = new QListWidget(splitter);
  m_itemList->setViewMode(QListWidget::IconMode);
  m_itemList->setIconSize({64, 64});
  m_itemList->setResizeMode(QListWidget::Adjust);
  m_itemList->setMovement(QListWidget::Static);
  m_itemList->setSpacing(8);
  // Force white background regardless the system theme. This helps to visualize better the images
  m_itemList->setStyleSheet(
      "QListWidget { background-color: white; }"
      "QListWidget::item { color: black; }"
      );
  m_itemList->setAutoFillBackground(true);

  splitter->addWidget(m_categoryList);
  splitter->addWidget(m_itemList);
  splitter->setStretchFactor(1, 1);
  mainLayout->addWidget(splitter);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  mainLayout->addWidget(buttons);

  connect(m_categoryList, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* cur, QListWidgetItem*) { onCategorySelected(cur); });
  connect(m_itemList, &QListWidget::itemDoubleClicked,
          this, &SvgGalleryDialog::onItemActivated);
  connect(m_filterEdit, &QLineEdit::textChanged,
          this, &SvgGalleryDialog::onFilterTextChanged);
  connect(m_libraryPathButton, &QPushButton::clicked,
          this, &SvgGalleryDialog::onSetLibraryPathClicked);
  connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
    if (auto* item = m_itemList->currentItem()){ onItemActivated(item);
    } else reject();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  m_userLibraryPath = loadUserLibraryPath();
  m_libraryPathLabel->setText(tr("User library: %1").arg(m_userLibraryPath));

  populateGallery();
}

void SvgGalleryDialog::scanGalleryRoot(const QString& root) {
  QDir rootDir(root);
  if (!rootDir.exists()) return;

         // Files directly at the root -> "General" category.
  const QStringList rootSvgs = rootDir.entryList({"*.svg"}, QDir::Files);
  for (const QString& fname : rootSvgs) {
    m_categoryFiles[kGeneralCategory].append(rootDir.filePath(fname));
  }

         // Each immediate subdirectory is a category; merge into any
         // same-named category already found in a previously scanned root.
  const QStringList subdirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString& sub : subdirs) {
    QDir categoryDir(rootDir.filePath(sub));
    const QStringList svgs = categoryDir.entryList({"*.svg"}, QDir::Files);
    for (const QString& fname : svgs) {
      m_categoryFiles[sub].append(categoryDir.filePath(fname));
    }
  }
}

void SvgGalleryDialog::populateCategoryList() {
  m_categoryList->clear();
  m_categoryList->addItem(kAllCategory);

  QStringList categories = m_categoryFiles.keys();
  categories.sort(Qt::CaseInsensitive);
  m_categoryList->addItems(categories);

  m_categoryList->setCurrentRow(0); // default to "All"
}

void SvgGalleryDialog::populateGallery() {
  m_categoryFiles.clear();

  // Bundled gallery, installed alongside symbol files
  scanGalleryRoot(QucsSettings.SvgGalleryDir);

  // User-configurable library folder (persisted via QSettings, default
  // ~/QucsWorkspace/symbols/
  if (!m_userLibraryPath.isEmpty()) {
  scanGalleryRoot(m_userLibraryPath);
  }

  populateCategoryList();
}

void SvgGalleryDialog::populateItemsForCategory(const QString& category) {
  m_currentCategory = category;
  m_itemList->clear();

  QStringList files;
  if (category == kAllCategory) {
    for (const auto& list : std::as_const(m_categoryFiles)) files += list;
  } else {
    files = m_categoryFiles.value(category);
  }

  const QString filter = m_filterEdit->text().trimmed();

  for (const QString& path : std::as_const(files)) {
    QString baseName = QFileInfo(path).completeBaseName();
    if (!filter.isEmpty() && !baseName.contains(filter, Qt::CaseInsensitive))
      continue;

    auto* item = new QListWidgetItem(QIcon(renderThumbnail(path, {64, 64})), baseName);
    item->setData(Qt::UserRole, path);
    m_itemList->addItem(item);
  }
}

void SvgGalleryDialog::onCategorySelected(QListWidgetItem* item) {
  if (item) populateItemsForCategory(item->text());
}

void SvgGalleryDialog::onFilterTextChanged(const QString&) {
  // Re-apply the current category with the updated filter text.
  populateItemsForCategory(m_currentCategory);
}

void SvgGalleryDialog::onItemActivated(QListWidgetItem* item) {
  if (!item) return;
  m_selectedPath = item->data(Qt::UserRole).toString();
  accept();
}


QPixmap SvgGalleryDialog::renderThumbnail(const QString& svgPath, const QSize& size) const {
  QSvgRenderer renderer(svgPath);
  if (!renderer.isValid()) return QPixmap();

  QPixmap thumb(size);
  thumb.fill(Qt::transparent);
  QPainter painter(&thumb);
  renderer.render(&painter, thumb.rect());
  return thumb;
}

QString SvgGalleryDialog::loadUserLibraryPath() const {
  QSettings settings;
  QString defaultPath = QDir::homePath() + "/QucsWorkspace/symbols/";
  return settings.value(kSettingsKey, defaultPath).toString();
}

void SvgGalleryDialog::saveUserLibraryPath(const QString& path) {
  QSettings settings;
  settings.setValue(kSettingsKey, path);
}

void SvgGalleryDialog::onSetLibraryPathClicked() {

  // Fall back to the user home directory if the library path doesn't exist yet
  QString startDir;
  if (QDir(m_userLibraryPath).exists()) {
    startDir = m_userLibraryPath;
  } else {
    startDir = QDir::homePath() + "/QucsWorkspace/symbols/";
  }

  QString chosen = QFileDialog::getExistingDirectory(
      this, tr("Select SVG Library Folder"), startDir,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (chosen.isEmpty()) return; // user cancelled

  m_userLibraryPath = chosen;
  saveUserLibraryPath(chosen);
  m_libraryPathLabel->setText(tr("User library: %1").arg(m_userLibraryPath));

  // Rescan with the new library path and refresh whatever category is
  // currently shown, so the change is visible immediately.
  populateGallery();
  populateItemsForCategory(m_currentCategory.isEmpty() ? kAllCategory : m_currentCategory);
}