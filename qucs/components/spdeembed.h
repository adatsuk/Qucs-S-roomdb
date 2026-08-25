/// @file spdeembed.h
/// @brief S-parameter network N-port de-embedding component (definition)
/// @author Qucs Team; Andrés Martínez Mera
/// @date 2017-2026
/// @copyright Copyright (C) 2017 Qucs Team,
///            Based on sparamfile.cpp (C) 2003 by Michael Margraf
///            2026 Andrés Martínez Mera
///            Code porting from Qucs PR#693; GPLv3-or-later
/// @license GPL-3.0-or-later

#ifndef SPDEEMBED_H
#define SPDEEMBED_H

#include "component.h"
#include "componentdialog.h"
#include <QMessageBox>

/// @class SPDeEmbedDialog
/// @brief Custom dialog for the S-parameter de-embedding component
/// @details Extends ComponentDialog to automatically fill the Ports field
/// when the user selects a Touchstone file whose extension encodes the
/// port count (e.g. .s4p → 4 ports).
class SPDeEmbedDialog : public ComponentDialog {
  Q_OBJECT
public:
  /// @brief Constructor
  /// @param c  The SPDeEmbed component being edited.
  /// @param s  The schematic that owns the component.
  SPDeEmbedDialog(Component* c, Schematic* s);
};


/// @class SPDeEmbed
/// @details S-parameter de-embedding component
class SPDeEmbed : public MultiViewComponent  {
public:
  /// @brief Constructor
  SPDeEmbed();

  /// @brief Destructor
  ~SPDeEmbed() {};

  /// @brief Creates a new instance of this component.
  Component* newOne() override;

  /// @brief Provides component metadata and optionally creates a new instance.
  static Element* info(QString&, char* &, bool getNewOne=false);

  /// @brief Creates the custom properties dialog.
  /// @param s  The schematic that owns the component.
  /// @return SPDeEmbedDialog object
  ComponentDialog* createDialog(Schematic* s) override;

  /// @brief Parses the port count from a Touchstone filename extension.
  /// @param filename  Full or relative path to the Touchstone file.
  /// @return Port: number of ports
  static int portsFromFilename(const QString& filename);

  /// @brief Returns the absolute path to the Touchstone file.
  QString getSubcircuitFile() override;

protected:

  /// @brief Generates the Qucsator netlist line for this component.
  /// @return netlist
  QString netlist() override;

  /// @brief Draws the schematic symbol depending on @c Ports value.
  void createSymbol() override;
};

#endif /* SPDEEMBED_H */
