/// @file imagepainting.h
/// @brief Image painting component class (definition)
/// @author Andrés Martínez Mera
/// @date August 04, 2026

#ifndef IMAGEPAINTING_H
#define IMAGEPAINTING_H

#include "rectangle.h"
#include <QColor>
#include <QPen>
#include <QPixmap>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QDebug>
#include <QPainter>
#include <QObject>
#include <QComboBox>
#include <QCheckBox>
#include <QApplication>
#include <QSvgRenderer>


/// @brief Painting component to display SVG or raster images in a schematic
/// @class ImagePainting
/// @details Content can be embedded directly in the schematic file
/// (base64-encoded) or referenced by an external file path.
/// SVG content is detected automatically from the decoded bytes
/// and rendered vectorially via QSvgRenderer; all other supported formats
/// are rasterized into a QPixmap
class ImagePainting : public QObject, public qucs::Rectangle {
  Q_OBJECT
public:
  /// @brief Constructor
  ImagePainting();

  /// @brief Creates a new (and empty) instance of this class
  Painting* newOne() override;

  /// @brief Draws the image into the given painter
  /// @param painter Target painter
  void paint(QPainter* painter) override;

  /// @brief Parses the painting from the schematic file format
  /// @param s Image data saved in the schematic
  bool load(const QString& s) override;

  /// @brief Prepare (serialize) the image data for saving it in an schematic
  QString save() override;

  QString saveCpp() override;

  /// @brief Serialize the painting in JSON
  QString saveJSON() override;

  /// @brief Dialog for showing the object properties
  bool Dialog(QWidget* parent = nullptr) override;

  /// @brief Provide metadata used by the component/painting registry.
  /// @param Name [out] Human-readable display name ("Image").
  /// @param BitmapFile [out] Internal type identifier string used in
  ///  serialization ("ImagePainting").
  /// @param getNewOne If true, also return a freshly constructed
  /// instance; if false, only fill in the metadata.
  /// @return A new ImagePainting instance if getNewOne is true, otherwise nullptr.
  static Element* info(QString& Name, char* &BitmapFile, bool getNewOne = false);

  /// @brief Set the image directly from a QPixmap object
  void setImageFromPixmap(const QPixmap& pixmap);

  /// @brief Set the image from a file on disk
  void setImageFromPath(const QString& path);

  /// @brief Set the image from the system clipboard
  /// @note The clipboard must contain image data
  void setImageFromClipboard();

  // Override selection and interaction methods
  /// @brief Test whether a click point falls within this painting's selectable area.
  /// @param click Point to test, in schematic coordinates.
  /// @param tolerance Extra margin (in schematic units) added around the bounding box.
  /// @return true if the point is within the (expanded) bounding box.
  bool getSelected(const QPoint& click, int tolerance) override;

  /// @brief Test whether a click point is near a resize handle.
  /// @param click Point to test, in schematic coordinates.
  /// @param tolerance Hit-test tolerance around each handle.
  /// @return true if a resize handle is within tolerance of the click.
  bool resizeTouched(const QPoint& click, int tolerance) override;

  /// @brief Update preview geometry while dragging
  void MouseMoving(const QPoint& onGrid, Schematic* sch, const QPoint& cursor) override;

  /// @brief Handle the mouse press event
  bool MousePressing(Schematic* sch = nullptr) override;

  /// @brief Update this painting's bounding box while a resize handle is being dragged.
  /// @param x Current cursor x position, in schematic coordinates.
  /// @param y Current cursor y position, in schematic coordinates.
  /// @param p Schematic being edited.
  /// @details When aspect-ratio lock is enabled, determines which
  ///          corner is being dragged and constrains the resulting
  ///          width/height to preserve m_aspectRatio.
  void MouseResizeMoving(int x, int y, Schematic* p) override;

  /// @brief Clear internal state tracking which corner is being dragged during an aspect-ratio-locked resize.
  void ResetDragTracking();
  bool rotate() noexcept override;
  bool rotate(int xc, int yc) noexcept override;

  /// @brief Get the original width of the image
  /// @return width in pixels
  int getImageWidth() const;

  /// @brief Get the original height of the image
  /// @return height in pixels
  int getImageHeight() const;

private:
  QString imagePath;

  /// SVG @{
  // Raw bytes of the embedded asset exactly as read from disk/base64.
  // The image can't be stored as a QPixmap because for SVG the XML is needed
  QByteArray m_rawData;


  // Owns the decode + draw logic (SVG-vs-raster detection, QSvgRenderer
  // or QPixmap)
  std::unique_ptr<qucs::Image> m_renderable;

  /// @}

  /// @brief Ensure the currently referenced external image (if any) is decoded into memory.
  void loadImage();

  /// @brief Load image data
  /// @param data Image data, either XML SVG data or QPixmap
  /// @return false if something went wrong, true if it loaded ok
  /// @details Shared decode path used by both load() (base64 from a .sch file) and
  /// loadImage() (reading an external file referenced by imagePath).
  /// This avoids duplicating the SVG-vs-raster branch logic.
  bool loadFromRawData(const QByteArray& data);

  enum DraggedCorner { TopLeft, TopRight, BottomLeft, BottomRight, NotSet };
  DraggedCorner m_draggedCorner = NotSet;
  int m_lastDragX = -1;
  int m_lastDragY = -1;

  // Local pen properties
  QColor penColor;
  int penWidth;
  Qt::PenStyle penStyle;
  bool m_filled;

  // Aspect ratio control
  bool m_keepAspectRatio;
  double m_aspectRatio; // cached aspect ratio

  // Dialog widget members
  QLineEdit* m_pathEdit;
  QLineEdit* m_widthEdit;
  QLineEdit* m_heightEdit;
  QCheckBox* m_aspectRatioCheck;
  QPushButton* m_resetButton;
  QLabel* m_statusLabel;

  // Dialog handler methods
  /// @brief Slot: open a file picker for choosing an image/SVG source.
  void onBrowseClicked();

  /// @brief Slot: open the SVG symbol gallery for SVG templates
  /// @note The gallery is intended to provide templates for symbols
  void onGalleryClicked();

  /// @brief Slot: reset the width/height fields in the properties dialog
  /// to the original dimensions.
  void onResetClicked();

  /// @brief Slot: handle toggling of the "Keep aspect ratio" checkbox
  void onAspectRatioToggled(bool checked);


  /// @brief Slot: react to the image path field changing in the properties dialog
  /// @param newPath The updated path text.
  void onPathChanged(const QString& newPath);

  /// @brief Recompute the height field in the properties dialog from the current width field,
  /// using the original aspect ratio
  void updateHeight();

  // Helper methods
  void updateAspectRatio();

  /// @brief Compute a height that preserves the cached aspect ratio for a given target width.
  /// @param newWidth  Proposed new width
  /// @param newHeight Set to newWidth * m_aspectRatio.
  void applyAspectRatioToResize(int& newWidth, int& newHeight);
};

#endif // IMAGEPAINTING_H
