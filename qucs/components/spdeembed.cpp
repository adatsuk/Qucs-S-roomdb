/// @file spdeembed.cpp
/// @brief S-parameter network N-port de-embedding component (implementation)
/// @author Qucs Team; Andrés Martínez Mera
/// @date 2017-2026
/// @copyright Copyright (C) 2017 Qucs Team,
///            Based on sparamfile.cpp (C) 2003 by Michael Margraf
///            2026 Andrés Martínez Mera
///            Code porting from Qucs PR#693; GPLv3-or-later
/// @license GPL-3.0-or-later

#include "spdeembed.h"
#include "main.h" // for QucsSettings
#include "schematic.h"
#include "misc.h"

#include <QFileInfo>


SPDeEmbed::SPDeEmbed()
{
  Description = QObject::tr("S parameter file de-embedding");
  Simulator = spicecompat::simQucsator; // qucsator-RF only
  Model = "SPDfile";
  Name  = "XD";

         // must be the first property !!!
  Props.append(new Property("File", "test.s2p", true,
                            QObject::tr("name of the s parameter file")));
  Props.append(new Property("Data", "rectangular", false,
                            QObject::tr("data type")+" [rectangular, polar]"));
  Props.append(new Property("Interpolator", "linear", false,
                            QObject::tr("interpolation type")+" [linear, cubic]"));
  Props.append(new Property("duringDC", "open", false,
                            QObject::tr("representation during DC analysis")+
                                " [open, short, shortall, unspecified]"));

         // must be the last property !!!
  Props.append(new Property("Ports", "2", false,
                            QObject::tr("number of ports")));

  SPDeEmbed::createSymbol();
}

// -------------------------------------------------------
Component* SPDeEmbed::newOne()
{
  SPDeEmbed* p = new SPDeEmbed();
  return p;
}

Element* SPDeEmbed::info(QString& Name, char* &BitmapFile, bool getNewOne)
{
  // "de-embedding" at the beginning, so it's always visible in the component dock
  // to help distinguish it from the regular embedding component
  Name = QObject::tr("de-embedding n-port S parameter file");
  BitmapFile = (char *) "spdfile2";

  if(getNewOne) {
    SPDeEmbed* p = new SPDeEmbed();
    p->Props.first()->Value = "test.s2p";
    p->Props.last()->Value = "2";
    return p;
  }
  return 0;
}

// -------------------------------------------------------
QString SPDeEmbed::getSubcircuitFile()
{
  // construct full filename
  QString FileName = Props.first()->Value;
  return misc::properAbsFileName(FileName);
}

// -------------------------------------------------------
QString SPDeEmbed::netlist()
{
  QString s = Model+":"+Name;

  // output all node names
  foreach(Port *p1, Ports)
    s += " "+p1->Connection->Name;   // node names

  // output all properties
  Property *p2 = Props.first();
  s += " "+p2->Name+"=\"{"+getSubcircuitFile()+"}\"";

  // data type
  p2 = Props.at(1);
  s += " "+p2->Name+"=\""+p2->Value+"\"";

  // interpolator type
  p2 = Props.at(2);
  s += " "+p2->Name+"=\""+p2->Value+"\"";

  // DC property
  p2 = Props.at(3);
  s += " "+p2->Name+"=\""+p2->Value+"\"\n";

  return s;
}

// -------------------------------------------------------
void SPDeEmbed::createSymbol()
{
  QFont Font(QucsSettings.font); // default application font
  // symbol text is smaller (10 pt default)
  Font.setPointSize(10 );
  // get the small font size; use the screen-compatible metric
  QFontMetrics  smallmetrics(Font, 0);
  int fHeight = smallmetrics.lineSpacing();
  QString stmp;

  int w, PortDistance = 60;
  int Num = Props.last()->Value.toInt();

  // adjust number of ports: force even port number
  Num = 2 * (Num / 2);
  if(Num < 2){
    Num = 2;
  } else if(Num > 8) {
    PortDistance = 20;
    if(Num > 40) Num = 40;
  }
  Props.last()->Value = QString::number(Num);

         // draw symbol outline
  int h = (PortDistance/2)*((Num-1)/2) + 15;
  QPen pen(Qt::darkBlue, 2, Qt::DashLine);
  Lines.append(new qucs::Line(-15, -h, 15, -h, pen));
  Lines.append(new qucs::Line( 15, -h, 15,  h, pen));
  Lines.append(new qucs::Line(-15,  h, 15,  h, pen));
  Lines.append(new qucs::Line(-15, -h,-15,  h, pen));
  stmp = QObject::tr("file");
  w = smallmetrics.horizontalAdvance(stmp); // compute text size to center it
  Texts.append(new Text(-w/2, -fHeight/2, stmp));

  int i=0, y = 15-h;
  while(i<Num) { // add ports lines and numbers
    i++;
    Lines.append(new qucs::Line(-30, y,-15, y,QPen(Qt::darkBlue,2)));
    Ports.append(new Port(-30, y));
    stmp = QString::number(i);
    w = smallmetrics.horizontalAdvance(stmp);
    Texts.append(new Text(-25-w, y-fHeight-2, stmp)); // text right-aligned

    if(i == Num) break; // if odd number of ports there will be one port less on the right side
    i++;
    Lines.append(new qucs::Line( 15, y, 30, y,QPen(Qt::darkBlue,2)));
    Ports.append(new Port( 30, y));
    stmp = QString::number(i);
    Texts.append(new Text(25, y-fHeight-2, stmp)); // text left-aligned
    y += PortDistance;
  }

  Lines.append(new qucs::Line( 0, h, 0,h+15,QPen(Qt::darkBlue,2)));
  Texts.append(new Text( 4, h,"Ref"));
  Ports.append(new Port( 0,h+15));    // 'Ref' port

  x1 = -30; y1 = -h-2;
  x2 =  30; y2 =  h+15;
  // compute component name text position - normal size font
  QFontMetrics  metrics(QucsSettings.font, 0);   // use the screen-compatible metric
  tx = x1+4;
  ty = y1 - 2*metrics.lineSpacing() - 4;
}


ComponentDialog* SPDeEmbed::createDialog(Schematic* s)
{
  return new SPDeEmbedDialog(this, s);
}

int SPDeEmbed::portsFromFilename(const QString& filename)
{
  QFileInfo fi(filename);
  QString ext = fi.suffix().toLower();  // e.g. "s4p"
  if (!ext.startsWith('s') || !ext.endsWith('p'))
    return -1;
  QString middle = ext.mid(1, ext.length() - 2);  // strip leading 's' and trailing 'p'
  bool ok = false;
  int n = middle.toInt(&ok);
  return (ok && n >= 1) ? n : -1;
}

SPDeEmbedDialog::SPDeEmbedDialog(Component* c, Schematic* s)
    : ComponentDialog(c, s)
{
  // The base class constructor has already built the full property table.
  QTableWidget* table = findChild<QTableWidget*>();
  if (!table) return;

  int fileRow  = -1;
  int portsRow = -1;
  // Walk the table rows to find the "File" and "Ports" properties by name.
  for (int row = 0; row < table->rowCount(); ++row) {
    QTableWidgetItem* nameItem = table->item(row, 0);
    if (!nameItem) continue;
    if (nameItem->text() == "File")  fileRow  = row;
    if (nameItem->text() == "Ports") portsRow = row;
  }

  if (fileRow < 0 || portsRow < 0) return;

  QWidget* cellWidget = table->cellWidget(fileRow, 1);
  if (!cellWidget) return;
  QLineEdit* fileEdit = cellWidget->findChild<QLineEdit*>();
  if (!fileEdit) return;

  // Note: The lambda function here is the most simple way to check the number of ports. "table", "fileEdit" and "ports" are
  // not member variables
  connect(fileEdit, &QLineEdit::textChanged, this, [this, table, portsRow, fileEdit](const QString& filename) {
    // Get the number of ports from the .snp extension
    int n = SPDeEmbed::portsFromFilename(filename);
    if (n < 1) return;

    // Port count must be even
    if (n % 2 != 0) {
      QMessageBox::critical(this,
                            QObject::tr("Invalid port count"),
                            QObject::tr("The file \"%1\" specifies %2 port(s).\n\n"
                                        "De-embedding requires an even number of ports.")
                                .arg(QFileInfo(filename).fileName())
                                .arg(n));

      // Block the signal temporarily to avoid re-triggering this handler,
      // then clear the file field so the invalid file is not accepted.
      fileEdit->blockSignals(true);
      fileEdit->clear();
      fileEdit->blockSignals(false);
      return;
    }


    QString value = QString::number(n);

    // Update the QTableWidgetItem text — this is what slotApplyButton reads back.
    QTableWidgetItem* item = table->item(portsRow, 1);
    if (item) item->setText(value);

    // Update the port number widget.
    QLineEdit* portsEdit = qobject_cast<QLineEdit*>(table->cellWidget(portsRow, 1));
    if (portsEdit) portsEdit->setText(value);
  });
}
