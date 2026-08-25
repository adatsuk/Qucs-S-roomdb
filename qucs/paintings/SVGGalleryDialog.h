/// @file SvgGalleryDialog.h
/// @brief Gallery for SVG symbols (class definition)
/// @authors Andrés Martínez Mera - andresmmera@protonmail.com
/// @date Aug 6, 2026

#ifndef SVGGALLERYDIALOG_H
#define SVGGALLERYDIALOG_H

#include <QDialog>
#include <QListWidgetItem>
#include <QListWidget>
#include <QMap>
#include <QStringList>
#include <QPixmap>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QSplitter>
#include <QDialogButtonBox>
#include <QSvgRenderer>
#include <QLabel>
#include <QPushButton>

/// @brief Dialog presenting a browsable, categorized gallery of SVG
///        symbols for insertion into a schematic/symbol as an
///        ImagePainting.
/// @details Categories are derived from subfolder names under each
///          gallery root (bundled resource + user directory); same-
///          named subfolders across roots are merged into one
///          category. Files placed directly at a gallery root fall
///          into a catch-all "General" category.
class SvgGalleryDialog : public QDialog {
    Q_OBJECT
public:
    explicit SvgGalleryDialog(QWidget* parent = nullptr);

    /// @brief Absolute path of the SVG the user selected and accepted.
    /// @return Empty string if the dialog was cancelled.
    QString selectedFilePath() const { return m_selectedPath; }

private slots:
    void onCategorySelected(QListWidgetItem* item);
    void onItemActivated(QListWidgetItem* item);   // double-click = accept
    void onFilterTextChanged(const QString& text);

    void onSetLibraryPathClicked();

private:
    /// @brief Scan both gallery roots and build m_categoryFiles.
    void populateGallery();

    /// @brief Fill the left-hand category list from m_categoryFiles keys,
    ///        with a synthetic "All" entry at the top.
    void populateCategoryList();

    /// @brief Fill the right-hand thumbnail grid for one category,
    ///        applying the current search filter text if any.
    /// @param category Category name, or "All" to show every symbol.
    void populateItemsForCategory(const QString& category);

    QPixmap renderThumbnail(const QString& svgPath, const QSize& size) const;

    /// @brief Recursively scan `root` for category subfolders and SVG
    ///        files, merging results into m_categoryFiles.
    /// @param root Gallery root directory (bundled resource or user dir).
    void scanGalleryRoot(const QString& root);

    QString loadUserLibraryPath() const;
    void saveUserLibraryPath(const QString& path);

    QListWidget* m_categoryList;   // left sidebar
    QListWidget* m_itemList;       // right thumbnail grid
    QLineEdit*   m_filterEdit;
    QPushButton* m_libraryPathButton; ///< "Set Library Path..." button.
    QLabel*      m_libraryPathLabel;

    QString m_selectedPath;
    QString m_currentCategory;
    QString m_userLibraryPath;

    /// category name -> list of absolute SVG file paths belonging to it
    QMap<QString, QStringList> m_categoryFiles;

    static constexpr const char* kGeneralCategory = "General";
    static constexpr const char* kAllCategory = "All";
    static constexpr const char* kSettingsKey = "SvgGallery/UserLibraryPath";
};

#endif