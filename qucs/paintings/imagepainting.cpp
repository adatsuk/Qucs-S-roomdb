/// @file imagepainting.h
/// @brief Image painting component class (implementation)
/// @author Andrés Martínez Mera
/// @date August 04, 2026

#include "imagepainting.h"
#include "SVGGalleryDialog.h" // Image templates
#include "misc.h"
#include "schematic.h"


ImagePainting::ImagePainting() :
      Rectangle(false),
      penColor(Qt::black),
      penWidth(1),
      penStyle(Qt::SolidLine),
      m_keepAspectRatio(true),
      m_aspectRatio(1.0)
{
  Name = "ImagePainting ";
}

Painting* ImagePainting::newOne()
{
  return new ImagePainting();
}


void ImagePainting::paint(QPainter* painter) {
  loadImage();
  bool drewSomething = false;

  if (!boundingRect().isEmpty() && m_renderable && m_renderable->isValid()) {
    // Keep the delegate's box in sync with the current bounding rect,
    // which may have changed since load (resize/rotate/move).
    QRect br = boundingRect();
    m_renderable->x = br.left();
    m_renderable->y = br.top();
    m_renderable->w = br.width();
    m_renderable->h = br.height();

    painter->save();
    painter->setPen(Qt::NoPen);
    m_renderable->draw(painter);
    painter->restore();
    drewSomething = true;
  }

  if (!drewSomething) {
    if (x1 == x2) x2 = x1 + 100;
    if (y1 == y2) y2 = y1 + 100;
    Rectangle::paint(painter);
  }

  if (isSelected) {
    painter->setPen(QPen(Qt::darkGray, penWidth + 5));
    painter->drawRect(boundingRect());
    painter->setPen(QPen(Qt::white, penWidth, penStyle));
    painter->drawRect(boundingRect());
    const auto bounds = boundingRect().marginsAdded({0, 0, 1, 1});
    misc::draw_resize_handle(painter, bounds.topLeft());
    misc::draw_resize_handle(painter, bounds.topRight());
    misc::draw_resize_handle(painter, bounds.bottomRight());
    misc::draw_resize_handle(painter, bounds.bottomLeft());
  }
}


bool ImagePainting::load(const QString& s) {
  QStringList parts = s.split(' ', Qt::SkipEmptyParts);
  if (parts.size() < 6) return false;
  if (parts[0] != "ImagePainting") return false;

  bool ok;
  x1 = parts[1].toInt(&ok); if (!ok) return false;
  y1 = parts[2].toInt(&ok); if (!ok) return false;
  x2 = parts[3].toInt(&ok); if (!ok) return false;
  y2 = parts[4].toInt(&ok); if (!ok) return false;

  QString imageData = parts[5];

  imagePath.clear();
  m_renderable.reset();
  m_rawData.clear();

  if (imageData.isEmpty()) return false;
  QByteArray byteArray = QByteArray::fromBase64(imageData.toUtf8());
  if (byteArray.isEmpty()) return false;

  return loadFromRawData(byteArray);
}

QString ImagePainting::save() {
  QByteArray raw = m_rawData;
  if (raw.isEmpty() && !imagePath.isEmpty()) {
    QFile f(imagePath);
    if (f.open(QIODevice::ReadOnly)) raw = f.readAll();
  }

  QString imageData = raw.isEmpty() ? QString() : QString::fromLatin1(raw.toBase64());
  return QString("ImagePainting %1 %2 %3 %4 %5")
      .arg(x1).arg(y1).arg(x2).arg(y2).arg(imageData);
}

QString ImagePainting::saveCpp() {
  // Customize as needed; example:
  return QString("new ImagePainting(%1, %2, %3, %4, \"%5\")")
      .arg(x1).arg(y1).arg(x2-x1).arg(y2-y1).arg(imagePath);
}

QString ImagePainting::saveJSON() {
  return QStringLiteral("{\"type\":\"ImagePainting\",\"image\":\"%1\",%2}")
      .arg(imagePath, Rectangle::saveJSON().mid(1)); // Merge with base JSON
}

// Override getSelected to handle image area
bool ImagePainting::getSelected(const QPoint& click, int tolerance) {
  // Always check if click is within bounds (whether filled or not)
  return boundingRect()
      .marginsAdded({tolerance, tolerance, tolerance, tolerance})
      .contains(click);
}

// Override resizeTouched to maintain resize functionality
bool ImagePainting::resizeTouched(const QPoint& click, int tolerance) {
  return Rectangle::resizeTouched(click, tolerance);
}

// Override mouse interaction methods
void ImagePainting::MouseMoving(const QPoint& onGrid, Schematic* sch, const QPoint& cursor) {
  // Get the cursor coordinates
  x1 = onGrid.x();
  y1 = onGrid.y();
  x2 = x1;
  y2 = y1;

  // Draw a symbol (two mountains) while hovering
  // Draw frame
  sch->PostPaintEvent(_Rect, cursor.x() + 13, cursor.y(), 105, 48, 0, 0, true);

  // Draw the sun (larger, in the top-right corner)
  sch->PostPaintEvent(_Ellipse, cursor.x() + 100, cursor.y() + 8, 12, 12, 0, 0, true); // (x, y, width, height)

  // Draw the mountain on the left
  sch->PostPaintEvent(_Line, cursor.x() + 15, cursor.y() + 44, cursor.x() + 45, cursor.y() + 12, 0, 0, true); // left base to peak
  sch->PostPaintEvent(_Line, cursor.x() + 45, cursor.y() + 12, cursor.x() + 75, cursor.y() + 44, 0, 0, true); // peak to right base

  // Draw the mountain on the right
  sch->PostPaintEvent(_Line, cursor.x() + 45, cursor.y() + 44, cursor.x() + 81, cursor.y() + 4, 0, 0, true); // left base to peak
  sch->PostPaintEvent(_Line, cursor.x() + 81, cursor.y() + 4, cursor.x() + 115, cursor.y() + 44, 0, 0, true); // peak to right base

  // Add a ground line
  sch->PostPaintEvent(_Line, cursor.x() + 15, cursor.y() + 44, cursor.x() + 115, cursor.y() + 44, 0, 0, true); // ground
}

bool ImagePainting::MousePressing(Schematic* sch) {
  if (imagePath.isEmpty()) {
    QWidget* parentWidget = sch ? sch->parentWidget() : nullptr;
    if (!parentWidget) {
      parentWidget = QApplication::activeWindow();
    }

    // If the content is empty, show the full properties dialog to let the user
    // whether to choose an image from the disk or use the SVG gallery
    bool accepted = Dialog(parentWidget);

    if (accepted == false){
      return false;
    }

    bool hasContent = m_renderable && m_renderable->isValid();

    if (hasContent == false){
      return false;
    }

    x2 = x1 + getImageWidth();
    y2 = y1 + getImageHeight();
    updateAspectRatio();
    if (sch) {
      snapToGrid(sch);
    }
  }
  return true;
}

void ImagePainting::MouseResizeMoving(int x, int y, Schematic* p) {
  if (m_keepAspectRatio && m_aspectRatio > 0) {
    // If this is the first call or position jumped significantly, determine the corner
    if (m_draggedCorner == NotSet || abs(x - m_lastDragX) > 50 || abs(y - m_lastDragY) > 50) {
      // Calculate distances to each corner
      int distToTopLeft = abs(x - x1) + abs(y - y1);
      int distToTopRight = abs(x - x2) + abs(y - y1);
      int distToBottomLeft = abs(x - x1) + abs(y - y2);
      int distToBottomRight = abs(x - x2) + abs(y - y2);

      // Find the minimum distance to determine which corner is being dragged
      int minDist = qMin(qMin(distToTopLeft, distToTopRight), qMin(distToBottomLeft, distToBottomRight));

      if (minDist == distToTopLeft) {
        m_draggedCorner = TopLeft;
      } else if (minDist == distToTopRight) {
        m_draggedCorner = TopRight;
      } else if (minDist == distToBottomLeft) {
        m_draggedCorner = BottomLeft;
      } else {
        m_draggedCorner = BottomRight;
      }
    }

    m_lastDragX = x;
    m_lastDragY = y;

    int constrainedX = x;
    int constrainedY = y;

    // Use the stored corner that was determined at drag start
    switch (m_draggedCorner) {
    case TopLeft: {
      int deltaX = x2 - x;
      int deltaY = y2 - y;
      if (deltaX <= 0 || deltaY <= 0) return;

      double scale = (double)deltaX / (x2 - x1);
      int newWidth = qRound((x2 - x1) * scale);
      int newHeight = qRound(newWidth * m_aspectRatio);
      constrainedX = x2 - newWidth;
      constrainedY = y2 - newHeight;
      break;
    }
    case TopRight: {
      int deltaX = x - x1;
      int deltaY = y2 - y;
      if (deltaX <= 0 || deltaY <= 0) return;

      double scale = (double)deltaX / (x2 - x1);
      int newWidth = qRound((x2 - x1) * scale);
      int newHeight = qRound(newWidth * m_aspectRatio);
      constrainedX = x1 + newWidth;
      constrainedY = y2 - newHeight;
      break;
    }
    case BottomLeft: {
      int deltaX = x2 - x;
      int deltaY = y - y1;
      if (deltaX <= 0 || deltaY <= 0) return;

      double scale = (double)deltaX / (x2 - x1);
      int newWidth = qRound((x2 - x1) * scale);
      int newHeight = qRound(newWidth * m_aspectRatio);
      constrainedX = x2 - newWidth;
      constrainedY = y1 + newHeight;
      break;
    }
    case BottomRight: {
      int deltaX = x - x1;
      int deltaY = y - y1;
      if (deltaX <= 0 || deltaY <= 0) return;

      double scale = (double)deltaX / (x2 - x1);
      int newWidth = qRound((x2 - x1) * scale);
      int newHeight = qRound(newWidth * m_aspectRatio);
      constrainedX = x1 + newWidth;
      constrainedY = y1 + newHeight;
      break;
    }
    default:
      break;
    }

    Rectangle::MouseResizeMoving(constrainedX, constrainedY, p);
  } else {
    Rectangle::MouseResizeMoving(x, y, p);
  }
}

void ImagePainting::ResetDragTracking() {
  m_draggedCorner = NotSet;
  m_lastDragX = -1;
  m_lastDragY = -1;
}


bool ImagePainting::Dialog(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setWindowTitle(QObject::tr("Image Properties"));
  auto* layout = new QVBoxLayout(&dialog);

  // Add image path UI
  auto* imageLayout = new QHBoxLayout;
  auto* pathLabel = new QLabel(QObject::tr("Image Path:"));
  m_pathEdit = new QLineEdit(imagePath);
  auto* browseButton = new QPushButton(QObject::tr("Browse..."));
  auto* galleryButton = new QPushButton(QObject::tr("Gallery..."));

  // Add status label to show if image is embedded or external
  m_statusLabel = new QLabel();
  if (m_renderable && m_renderable->isValid() && imagePath.isEmpty()) {
    m_statusLabel->setText(QObject::tr("Image embedded in schematic"));
    m_statusLabel->setStyleSheet("color: green; font-style: italic;");
  } else if (!imagePath.isEmpty()) {
    m_statusLabel->setText(QObject::tr("External image file"));
    m_statusLabel->setStyleSheet("color: blue; font-style: italic;");
  } else {
    m_statusLabel->setText(QObject::tr("No image loaded"));
    m_statusLabel->setStyleSheet("color: red; font-style: italic;");
  }

  // Connect browse button
  QObject::connect(browseButton, &QPushButton::clicked, this, &ImagePainting::onBrowseClicked);

  // Gallery
  QObject::connect(galleryButton, &QPushButton::clicked, this, &ImagePainting::onGalleryClicked);

  imageLayout->addWidget(pathLabel);
  imageLayout->addWidget(m_pathEdit);
  imageLayout->addWidget(browseButton);
  imageLayout->addWidget(galleryButton);

  // Add dimensions UI
  auto* dimensionsLayout = new QVBoxLayout;

  // Width input
  auto* widthLayout = new QHBoxLayout;
  auto* widthLabel = new QLabel(QObject::tr("Width:"));
  m_widthEdit = new QLineEdit(QString::number(x2 - x1));
  m_widthEdit->setValidator(new QIntValidator(1, 10000, &dialog));
  widthLayout->addWidget(widthLabel);
  widthLayout->addWidget(m_widthEdit);

  // Height input
  auto* heightLayout = new QHBoxLayout;
  auto* heightLabel = new QLabel(QObject::tr("Height:"));
  m_heightEdit = new QLineEdit(QString::number(y2 - y1));
  m_heightEdit->setValidator(new QIntValidator(1, 10000, &dialog));
  heightLayout->addWidget(heightLabel);
  heightLayout->addWidget(m_heightEdit);

  // Aspect ratio checkbox - initialize with current state
  m_aspectRatioCheck = new QCheckBox(QObject::tr("Keep aspect ratio"));
  m_aspectRatioCheck->setChecked(m_keepAspectRatio);

  // Reset to original button
  m_resetButton = new QPushButton(QObject::tr("Reset to original dimensions"));
  m_resetButton->setEnabled(m_renderable && m_renderable->isValid()); // Enable if image is loaded

  dimensionsLayout->addLayout(widthLayout);
  dimensionsLayout->addLayout(heightLayout);
  dimensionsLayout->addWidget(m_aspectRatioCheck);
  dimensionsLayout->addWidget(m_resetButton);

  // Connect signals to handlers
  QObject::connect(m_resetButton, &QPushButton::clicked, this, &ImagePainting::onResetClicked);
  QObject::connect(m_aspectRatioCheck, &QCheckBox::toggled, this, &ImagePainting::onAspectRatioToggled);
  QObject::connect(m_pathEdit, &QLineEdit::textChanged, this, &ImagePainting::onPathChanged);

  // Connect width change to height calculation when aspect ratio is locked
  QObject::connect(m_widthEdit, &QLineEdit::textChanged, this, [this]() {
    if (m_aspectRatioCheck->isChecked()) {
      updateHeight();
    }
  });

  layout->addWidget(m_statusLabel);
  layout->addLayout(imageLayout);
  layout->addLayout(dimensionsLayout);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  layout->addWidget(&buttons);

  if (dialog.exec() == QDialog::Rejected) return false;

  // Update image path and load new image if changed
  QString newPath = m_pathEdit->text();
  if (newPath != imagePath) {
    imagePath = newPath;
    if (!imagePath.isEmpty()) {
      // Load new image from file path
      m_renderable.reset();
      loadImage();
      updateAspectRatio(); // Update aspect ratio when new image is loaded
    }
    // If path is cleared but we have an embedded image, keep it
    // (imagePath will be empty but image will remain loaded)
  }

  // Update persistent aspect ratio setting
  m_keepAspectRatio = m_aspectRatioCheck->isChecked();

  // Update dimensions
  int newWidth = m_widthEdit->text().toInt();
  int newHeight = m_heightEdit->text().toInt();

  if (newWidth > 0 && newHeight > 0) {
    x2 = x1 + newWidth;
    y2 = y1 + newHeight;
  }

  return true;
}


Element* ImagePainting::info(QString& Name, char* &BitmapFile, bool getNewOne) {
  Name = QObject::tr("Image");
  BitmapFile = (char*)"ImagePainting";
  return getNewOne ? new ImagePainting() : nullptr;
}

void ImagePainting::loadImage() {
  if (m_renderable && m_renderable->isValid()) return;
  if (imagePath.isEmpty()) return;

  QFile f(imagePath);
  if (!f.open(QIODevice::ReadOnly)) {
    qWarning("Failed to open image file: %s", qUtf8Printable(imagePath));
    return;
  }
  loadFromRawData(f.readAll());
}
// Override rotate methods to maintain functionality
bool ImagePainting::rotate() noexcept {
  bool result = qucs::Rectangle::rotate();
  if (result && m_aspectRatio > 0) m_aspectRatio = 1.0 / m_aspectRatio;
  return result;
}

bool ImagePainting::rotate(int xc, int yc) noexcept {
  bool result = qucs::Rectangle::rotate(xc, yc);
  if (result && m_aspectRatio > 0) m_aspectRatio = 1.0 / m_aspectRatio;
  return result;
}


void ImagePainting::setImageFromPixmap(const QPixmap& pixmap) {
  if (pixmap.isNull()) return;
  imagePath.clear();

  QByteArray raw;
  QBuffer buffer(&raw);
  buffer.open(QIODevice::WriteOnly);
  pixmap.save(&buffer, "PNG");

  loadFromRawData(raw);
}

void ImagePainting::setImageFromPath(const QString& path) {
  if (path.isEmpty()) return;
  imagePath = path;
  m_renderable.reset();
  m_rawData.clear();
  loadImage();
}

void ImagePainting::setImageFromClipboard() {
  QClipboard* clipboard = QApplication::clipboard();
  if (clipboard->mimeData()->hasImage()) {
    QImage clipboardImage = clipboard->image();
    if (!clipboardImage.isNull()) {
      QPixmap pixmap = QPixmap::fromImage(clipboardImage);
      setImageFromPixmap(pixmap);
    }
  }
}


void ImagePainting::onBrowseClicked() {

  QString filter = QObject::tr("Images (*.bmp *.gif *.jpg *.jpeg *.png *.svg)");
  QString path = QFileDialog::getOpenFileName(
      m_pathEdit->parentWidget(),
      QObject::tr("Select Image"),
      QDir::homePath(),
      filter
      );


  if (!path.isEmpty()) {
    m_pathEdit->setText(path);
    m_statusLabel->setText(QObject::tr("External image file"));
    m_statusLabel->setStyleSheet("color: blue; font-style: italic;");
  }
}

void ImagePainting::onResetClicked() {
  QString currentPath = m_pathEdit->text();

  // First try to use already loaded image
  if (!(m_renderable && m_renderable->isValid()) && !currentPath.isEmpty()) {
    // Nothing embedded yet — try loading from the path
    QFile f(currentPath);
    if (f.open(QIODevice::ReadOnly)) {
      loadFromRawData(f.readAll());
    }
  }

  if (m_renderable && m_renderable->isValid()) {
    m_widthEdit->setText(QString::number(getImageWidth()));
    m_heightEdit->setText(QString::number(getImageHeight()));
    m_resetButton->setEnabled(true);
  }
}

void ImagePainting::onAspectRatioToggled(bool checked) {
  m_heightEdit->setEnabled(!checked);
  if (checked) {
    updateHeight();
  }
}

void ImagePainting::onPathChanged(const QString& newPath) {
  if (!newPath.isEmpty()) {
    QFile f(newPath);
    bool valid = false;
    if (f.open(QIODevice::ReadOnly)) {
      qucs::Image probe(0, 0, 1, 1, f.readAll());
      valid = probe.isValid();
    }

    m_resetButton->setEnabled(valid);
    if (valid && m_aspectRatioCheck->isChecked()) {
      updateHeight();
    }
  } else {
    m_resetButton->setEnabled(m_renderable && m_renderable->isValid());
  }
}

void ImagePainting::updateHeight() {
  if (m_aspectRatioCheck && m_aspectRatioCheck->isChecked()) {
    QSize nativeSize;

    if (m_renderable && m_renderable->isValid()) {
      nativeSize = m_renderable->nativeSize();
    } else if (m_pathEdit && !m_pathEdit->text().isEmpty()) {
      QFile f(m_pathEdit->text());
      if (f.open(QIODevice::ReadOnly)) {
        qucs::Image probe(0, 0, 1, 1, f.readAll());
        if (probe.isValid()) nativeSize = probe.nativeSize();
      }
    }

    if (nativeSize.isValid() && m_widthEdit && m_heightEdit) {
      int width = m_widthEdit->text().toInt();
      if (width > 0 && nativeSize.width() > 0) {
        double aspectRatio = (double)nativeSize.height() / nativeSize.width();
        int height = qRound(width * aspectRatio);
        m_heightEdit->setText(QString::number(height));
      }
    }
  }
}

void ImagePainting::updateAspectRatio() {
  if (m_renderable && m_renderable->isValid()) {
    QSize sz = m_renderable->nativeSize();
    m_aspectRatio = (sz.width() > 0) ? (double)sz.height() / sz.width() : 1.0;
  } else {
    m_aspectRatio = 1.0;
  }
}

void ImagePainting::applyAspectRatioToResize(int& newWidth, int& newHeight) {
  if (m_aspectRatio <= 0) return;

  // Calculate what the height should be based on width and aspect ratio
  int calculatedHeight = qRound(newWidth * m_aspectRatio);

  // Use the calculated height
  newHeight = calculatedHeight;
}

// Needed to have the image size at schematic.cpp when drag and dropping
int ImagePainting::getImageWidth() const {
  if (m_renderable && m_renderable->isValid()) {
    int w = m_renderable->nativeSize().width();
    return w > 0 ? w : 100;
  }
  return 100;
}

int ImagePainting::getImageHeight() const {
  if (m_renderable && m_renderable->isValid()) {
    int h = m_renderable->nativeSize().height();
    return h > 0 ? h : 100;
  }
  return 100;
}


bool ImagePainting::loadFromRawData(const QByteArray& data) {
  m_rawData = data;
  m_renderable = std::make_unique<qucs::Image>(x1, y1, x2 - x1, y2 - y1, data);

  if (!m_renderable->isValid()) {
    qWarning("Failed to load image data");
    m_renderable.reset();
    return false;
  }
  updateAspectRatio();
  return true;
}

void ImagePainting::onGalleryClicked() {
  QWidget* parentWidget = m_pathEdit ? m_pathEdit->parentWidget() : nullptr;
  if (!parentWidget) {
    parentWidget = QApplication::activeWindow();
  }

  SvgGalleryDialog gallery(parentWidget);
  if (gallery.exec() != QDialog::Accepted || gallery.selectedFilePath().isEmpty()) {
    return; // user cancelled the gallery — leave the current path untouched
  }


  m_pathEdit->setText(gallery.selectedFilePath());
}