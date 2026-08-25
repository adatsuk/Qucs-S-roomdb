/***************************************************************************
                                sparamfile.h
                               --------------
    begin                : Sat Aug 23 2003
    copyright            : (C) 2003 by Michael Margraf
    email                : michael.margraf@alumni.tu-berlin.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SPARAMFILE_H
#define SPARAMFILE_H

#include "component.h"

// Needed for the custom properties dialog to autoupdate the port count
#include "componentdialog.h"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QLineEdit>

/// @class SParamFileDialog
/// @brief Custom properties dialog for SParamFile.
/// @details Automatically fills the Ports field when the user selects
///          a Touchstone file according to its extension
class SParamFileDialog : public ComponentDialog {
  Q_OBJECT
public:
  /// @brief Constructor.
  /// @param c  The SParamFile component being edited.
  /// @param s  The schematic that owns the component.
  SParamFileDialog(Component* c, Schematic* s);
};

class SParamFile : public MultiViewComponent  {
public:
  SParamFile();
 ~SParamFile() {};
  Component* newOne() override;
  static Element* info(QString&, char* &, bool getNewOne=false);
  static Element* info1(QString&, char* &, bool getNewOne=false);
  static Element* info2(QString&, char* &, bool getNewOne=false);

  QString getSubcircuitFile() override;

  /// @brief Creates the custom properties dialog.
  /// @param s  The schematic that owns the component.
  /// @return   SParamFileDialog object.
  ComponentDialog* createDialog(Schematic* s) override;

  /// @brief Parses port count N from a .sNp filename extension.
  /// @param filename  Full or relative path to the Touchstone file.
  /// @return Port count (>= 1) on success, or -1 if not recognised.
  static int portsFromFilename(const QString& filename);

protected:
  QString netlist() override;
  void createSymbol() override;
  QString spice_netlist(spicecompat::SpiceDialect dialect = spicecompat::SPICEDefault) override;
};

#endif
