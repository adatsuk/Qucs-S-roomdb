/***************************************************************************
                               libcomp.cpp
                              -------------
    begin                : Fri Jun 10 2005
    copyright            : (C) 2005 by Michael Margraf
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

#include "libcomp.h"
#include "main.h"
#include "misc.h"
#include "node.h"
#include "extsimkernels/qucs2spice.h"
#include "extsimkernels/spicecompat.h"
#include "extsimkernels/abstractspicekernel.h"
#include "schematic.h"

#ifdef QUCS_ENABLE_CORE
#include "core_primitive_symbol.h"
#endif


#include <QTextStream>
#include <QPlainTextEdit>
#include <QMap>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QDebug>

LibComp::LibComp()
{
  Type = isComponent;   // both analog and digital
  Description = QObject::tr("Component taken from Qucs library");

  Ports.append(new Port(0,  0));  // dummy port because of being device

  Model = "Lib";
  Name  = "X";
  SpiceModel = "X";

  Props.append(new Property("Lib", "", true,
		QObject::tr("name of qucs library file")));
  Props.append(new Property("Comp", "", true,
		QObject::tr("name of component in library")));
}

// ---------------------------------------------------------------------
Component* LibComp::newOne()
{
  LibComp *p = new LibComp();
  p->Props.at(0)->Value = Props.at(0)->Value;
  p->Props.at(1)->Value = Props.at(1)->Value;
  p->recreate();
  return p;
}

namespace {

void renameIndexedLibProps(QList<Property *> &props)
{
    static const QStringList paramNames = {
        QStringLiteral("l"), QStringLiteral("w"), QStringLiteral("ng"), QStringLiteral("m"),
        QStringLiteral("nf"), QStringLiteral("nr")};
    int idx = 0;
    for (int i = 2; i < props.size() && idx < paramNames.size(); ++i, ++idx) {
        props.at(i)->Name = paramNames.at(idx);
        props.at(i)->Description = paramNames.at(idx);
    }
}

bool looksLikeOldNamedExport(const QList<Property *> &props)
{
    static const QStringList paramNames = {
        QStringLiteral("l"), QStringLiteral("w"), QStringLiteral("ng"), QStringLiteral("m"),
        QStringLiteral("nf"), QStringLiteral("nr")};
    return props.size() > 3 && paramNames.contains(props.at(2)->Value);
}

void migrateOldNamedLibProps(QList<Property *> &props)
{
    static const QStringList paramNames = {
        QStringLiteral("l"), QStringLiteral("w"), QStringLiteral("ng"), QStringLiteral("m"),
        QStringLiteral("nf"), QStringLiteral("nr")};
    QStringList values;
    for (int i = 2; i < props.size(); ++i) {
        if (((i - 2) % 2) == 0) {
            continue;
        }
        values.append(props.at(i)->Value);
    }
    while (props.size() > 2) {
        delete props.takeLast();
    }
    for (int i = 0; i < values.size() && i < paramNames.size(); ++i) {
        props.append(new Property(paramNames.at(i), values.at(i), true, paramNames.at(i)));
    }
}

} // namespace

void LibComp::normalizeLibProperties()
{
    if (Props.size() >= 2) {
        Props.at(0)->display = false;
        Props.at(1)->display = false;
    }
    if (looksLikeOldNamedExport(Props)) {
        migrateOldNamedLibProps(Props);
        return;
    }
    if (Props.size() > 2 && Props.at(2)->Name.startsWith(QLatin1Char('p'))) {
        renameIndexedLibProps(Props);
    }
}

// ---------------------------------------------------------------------
// Makes the schematic symbol subcircuit with the correct number
// of ports.
void LibComp::createSymbol()
{
  tx = INT_MIN;
  ty = INT_MIN;
  if(loadSymbol() > 0) {
    if(tx == INT_MIN)  tx = x1+4;
    if(ty == INT_MIN)  ty = y2+4;
  }
  else {
    // only paint a rectangle
    Lines.append(new qucs::Line(-15, -15, 15, -15, QPen(Qt::darkBlue,2)));
    Lines.append(new qucs::Line( 15, -15, 15,  15, QPen(Qt::darkBlue,2)));
    Lines.append(new qucs::Line(-15,  15, 15,  15, QPen(Qt::darkBlue,2)));
    Lines.append(new qucs::Line(-15, -15,-15,  15, QPen(Qt::darkBlue,2)));

    x1 = -18; y1 = -18;
    x2 =  18; y2 =  18;

    tx = x1+4;
    ty = y2+4;
  }
}

// ---------------------------------------------------------------------
// Open "<lib>.lib" from system LibDir or ~/.qucs/user_lib (IHP PDK lives there).
static bool openQucsLibraryFile(const QString &libName, Schematic *sch, QFile &file)
{
  QStringList dirs;
  dirs << QucsSettings.LibDir;
  dirs << QucsSettings.qucsWorkspaceDir.absoluteFilePath(QStringLiteral("user_lib"));
  dirs << QDir::home().absoluteFilePath(QStringLiteral(".qucs/user_lib"));

  const QString base = libName + QStringLiteral(".lib");
  for (const QString &dir : dirs) {
    if (dir.trimmed().isEmpty()) {
      continue;
    }
    const QString candidate = misc::properAbsFileName(QDir(dir).absoluteFilePath(base), sch);
    file.setFileName(candidate);
    if (file.open(QIODevice::ReadOnly)) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------
// Loads the section with name "Name" from library file into "Section".
int LibComp::loadSection(const QString& Name, QString& Section,
             QStringList *Includes, QStringList *Attach)
{
  QFile file;
  if (!openQucsLibraryFile(Props.at(0)->Value, containingSchematic, file))
    return -1;

  QString libDefaultSymbol;

  QTextStream ReadWhole(&file);
  Section = ReadWhole.readAll();
  file.close();


  if(Section.left(14) != "<Qucs Library ")  // wrong file type ?
    return -2;

  int Start, End = Section.indexOf(' ', 14);
  if(End < 15) return -3;
  QString Line = Section.mid(14, End-14); // extract version string
  VersionTriplet LibVersion = VersionTriplet(Line);
  if (LibVersion > QucsVersion) {// wrong version number ?
      if (!QucsSettings.IgnoreFutureVersion) {
          return -3;
      }
  }

  if(Name == "Symbol") {
    Start = Section.indexOf("\n<", 14); // library has default symbol
    if(Start > 0)
      if(Section.mid(Start+2, 14) == "DefaultSymbol>") {
        Start += 16;
        End = Section.indexOf("\n</DefaultSymbol>", Start);
        if(End < 0)  return -9;
        libDefaultSymbol = Section.mid(Start, End-Start);
      }
  }

  // search component
  Line = "\n<Component " + Props.at(1)->Value + ">";
  Start = Section.indexOf(Line);
  if(Start < 0)  return -4;  // component not found
  Start = Section.indexOf('\n', Start);
  if(Start < 0)  return -5;  // file corrupt
  Start++;
  End = Section.indexOf("\n</Component>", Start);
  if(End < 0)  return -6;  // file corrupt
  Section = Section.mid(Start, End-Start+1);
  
  // search model includes
  if(Includes) {
    int StartI, EndI;
    StartI = Section.indexOf("<"+Name+"Includes");
    if(StartI >= 0) {  // includes found
      StartI = Section.indexOf('"', StartI);
      if(StartI < 0)  return -10;  // file corrupt
      EndI = Section.indexOf('>', StartI);
      if(EndI < 0)  return -11;  // file corrupt
      StartI++; EndI--;
      QString inc = Section.mid(StartI, EndI-StartI);
      QStringList f = inc.split(QRegularExpression("\"\\s+\""));
      for(QStringList::Iterator it = f.begin(); it != f.end(); ++it ) {
	Includes->append(*it);
      }
    }
  }

  // search attached files
  if(Attach) {
    int StartI, EndI;
    StartI = Section.indexOf("<"+Name+"Attach");
    if(StartI >= 0) {  // includes found
      StartI = Section.indexOf('"', StartI);
      if(StartI < 0)  return -10;  // file corrupt
      EndI = Section.indexOf('>', StartI);
      if(EndI < 0)  return -11;  // file corrupt
      StartI++; EndI--;
      QString inc = Section.mid(StartI, EndI-StartI);
      QStringList f = inc.split(QRegularExpression("\"\\s+\""));
      for(QStringList::Iterator it = f.begin(); it != f.end(); ++it ) {
    Attach->append(*it);
      }
    }
  }

  // search model
  Start = Section.indexOf("<"+Name+">");
  if(Start < 0) {
    if((Name == "Symbol") && (!libDefaultSymbol.isEmpty())) {
      // component does not define its own symbol but the library defines a default symbol
      Section = libDefaultSymbol;
      return 0;
    } else {
      return -7;  // symbol not found
    }
  }
  Start = Section.indexOf('\n', Start);
  if(Start < 0)  return -8;  // file corrupt
  while(Section.at(++Start) == ' ') ;
  End = Section.indexOf("</"+Name+">", Start);
  if(End < 0)  return -9;  // file corrupt

  // snip actual model
  Section = Section.mid(Start, End-Start);
  return 0;
}

// ---------------------------------------------------------------------
// Loads the symbol for the subcircuit from the schematic file and
// returns the number of painting elements.
int LibComp::loadSymbol()
{
  int z, Result;
  QString FileString, Line;
#ifdef QUCS_ENABLE_CORE
  // Prefer CORE/commonLib (and PDK) symbol geometry when attached — pin centers must match
  // schematic wires stored on CORE/Xschem terminals, not legacy Qucs .lib artwork.
  {
    QString coreSymbol;
    if (qucs_core::tryLoadCorePrimitiveSymbol(Props.at(1)->Value, coreSymbol)) {
      FileString = coreSymbol;
      z = 0;
    } else {
      z = loadSection("Symbol", FileString);
    }
  }
#else
  z = loadSection("Symbol", FileString);
#endif
  if(z < 0) {
    if(z != -7)  return z;

    // If library component not defined as subcircuit, then load
    // new component and transfer data to this component.
    z = loadSection("Model", Line);
    if(z < 0)  return z;

    std::shared_ptr<Component> pc(getComponentFromName(Line));
    if(!pc)  return -20;
    copyComponent(pc.get());

    return 1;
  }


  z  = 0;
  x1 = y1 = INT_MAX;
  x2 = y2 = INT_MIN;

  QTextStream stream(&FileString, QIODevice::ReadOnly);
  while(!stream.atEnd()) {
    Line = stream.readLine();
    Line = Line.trimmed();
    if(Line.isEmpty())  continue;
    if(Line.at(0) != '<') return -11;
    if(Line.at(Line.length()-1) != '>') return -12;
    Line = Line.mid(1, Line.length()-2); // cut off start and end character
    Result = analyseLine(Line, 2);
    if(Result < 0) return -13;   // line format error
    z += Result;
  }

  x1 -= 4;  x2 += 4;   // enlarge component boundings a little
  y1 -= 4;  y2 += 4;
  return z;      // return number of ports
}

// -------------------------------------------------------
QString LibComp::getSubcircuitFile()
{
  QDir Directory(QucsSettings.LibDir);
  QString FileName = misc::properAbsFileName(Directory.absoluteFilePath(Props.first()->Value) + ".lib");
  FileName.chop(4);
  return FileName;
}

// -------------------------------------------------------
bool LibComp::createSubNetlist(QTextStream *stream, QStringList &FileList,
			       int type)
{
  int r = -1;
  QString FileString;
  QStringList Includes;

#ifdef QUCS_ENABLE_CORE
  // Hierarchical CORE cell (e.g. module_0_foundations/inverter): emit .SUBCKT from schematic.core.
  if ((type & 8) || (type & 16)) {
    QString coreSch;
    QString hint;
    if (containingSchematic) {
      hint = containingSchematic->getDocName();
    }
    if (Props.size() >= 2
        && qucs_core::tryResolveCoreSchematic(Props.at(0)->Value, Props.at(1)->Value, coreSch, hint)) {
      Schematic *d = new Schematic(nullptr, coreSch);
      if (d->loadDocument()) {
        for (Component *pc : d->a_DocComps) {
          if (pc) {
            pc->setSchematic(d);
          }
        }
        // Align Port Num with symbol pin order (by pin name / lab).
        QString symSection;
        QMap<QString, int> pinNameToNum;
        if (qucs_core::tryLoadCorePrimitiveSymbol(Props.at(1)->Value, symSection)) {
          const QStringList symLines = symSection.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
          for (const QString &raw : symLines) {
            QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('<'))) {
              line = line.mid(1);
            }
            if (line.endsWith(QLatin1Char('>'))) {
              line.chop(1);
            }
            if (!line.startsWith(QLatin1String(".PortSym")) && !line.startsWith(QLatin1String("PortSym"))) {
              continue;
            }
            // PortSym x y num angle name
            const QStringList tok = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (tok.size() >= 6) {
              bool ok = false;
              const int num = tok.at(3).toInt(&ok);
              if (ok && num > 0) {
                pinNameToNum.insert(tok.at(5), num);
              }
            }
          }
        }
        for (Component *pc : d->a_DocComps) {
          if (!pc || pc->Model != QLatin1String("Port") || pc->Props.isEmpty()) {
            continue;
          }
          const QString lab = pc->Props.first()->Value;
          if (pinNameToNum.contains(lab)) {
            pc->Props.first()->Value = QString::number(pinNameToNum.value(lab));
          }
        }

        // Instance spice_netlist uses createType() as subckt name — match that.
        const QString subName = createType();
        d->setDocName(subName);
        d->setIsAnalog(true);
        d->setIsVerilog(false);

        QString subText;
        QTextStream subStream(&subText, QIODevice::WriteOnly);
        AbstractSpiceKernel kern(d);
        QStringList incompat;
        if (!kern.checkSchematic(incompat)) {
          delete d;
          return false;
        }
        kern.createSubNetlist(subStream, false);
        delete d;

        // Previously we returned true even when prepareSpiceNetlist failed inside the
        // kernel and wrote nothing — that produced "unknown subckt" in ngspice.
        if (!subText.contains(QStringLiteral(".SUBCKT"), Qt::CaseInsensitive)
            && !subText.contains(QStringLiteral(".subckt"), Qt::CaseInsensitive)) {
          return false;
        }
        if (!subText.contains(subName, Qt::CaseInsensitive)) {
          return false;
        }
        (*stream) << subText;
        if (!subText.endsWith(QLatin1Char('\n'))) {
          (*stream) << '\n';
        }
        return true;
      }
      delete d;
    }
  }
#endif

  if(type&1) {
    r = loadSection("Model", FileString, &Includes);
  } else if(type&2) {
    r = loadSection("VHDLModel", FileString, &Includes);
  } else if(type&4) {
    r = loadSection("VerilogModel", FileString, &Includes);
  } else if(type&8) {
    r = loadSection("Spice",FileString, &Includes);
    if (r<0) {
        r = loadSection("Model", FileString, &Includes); // Ngspice
        FileString = qucs2spice::convert_netlist(FileString);
    }
  } else if (type&16) {
      r = loadSection("Spice",FileString, &Includes);
      if (r<0) {
          r = loadSection("Model", FileString, &Includes); // Ngspice
          FileString = qucs2spice::convert_netlist(FileString,true);
      }
  }
  if(r < 0)  return false;

  // also include files
  int error = 0;
  for(QStringList::Iterator it = Includes.begin();
      it != Includes.end(); ++it ) {
    QString s = getSubcircuitFile()+"/"+*it;
    if(FileList.indexOf(s) >= 0) continue;
    FileList.append(s);

    // load file and stuff into stream
    QFile file(s);
    if(!file.open(QIODevice::ReadOnly)) {
      error++;
    } else {
      QByteArray FileContent = file.readAll();
      file.close();
      //?stream->writeRawBytes(FileContent.data(), FileContent.size());
      (*stream) << FileContent.data();
      qDebug() << "hi from libcomp";
    }
  }

  (*stream) << "\n" << FileString << "\n";
  return error > 0 ? false : true;
}

// -------------------------------------------------------
QString LibComp::createType()
{
  QString Type = misc::properFileName(Props.at(0)->Value);
  return misc::properName(Type + "_" + Props.at(1)->Value);
}

// -------------------------------------------------------
QString LibComp::netlist()
{
  QString s = "Sub:"+Name;   // output as subcircuit

  // output all node names
  for (Port *p1 : std::as_const(Ports))
    s += " "+p1->Connection->Name;   // node names

  // output property
  s += " Type=\""+createType()+"\"";   // type for subcircuit

  // output user defined parameters
  for(int i = 2;i<Props.size();i++)
    s += " "+Props.at(i)->Name+"=\""+Props.at(i)->Value+"\"";

  return s + '\n';
}

// -------------------------------------------------------
QString LibComp::verilogCode(int)
{
  QString s = "  Sub_" + createType() + " " + Name + " (";

  // output all node names
  QListIterator<Port *> iport(Ports);
  Port *pp = iport.next();
  if(pp)  s += pp->Connection->Name;
  while (iport.hasNext()) {
    pp = iport.next();
    s += ", "+pp->Connection->Name;   // node names
  }

  s += ");\n";
  return s;
}

// -------------------------------------------------------
QString LibComp::vhdlCode(int)
{
  QString s = "  " + Name + ": entity Sub_" + createType() + " port map (";

  // output all node names
  QListIterator<Port *> iport(Ports);
  Port *pp = iport.next();
  if(pp)  s += pp->Connection->Name;
  while (iport.hasNext()) {
    pp = iport.next();
    s += ", "+pp->Connection->Name;   // node names
  }

  s += ");\n";
  return s;
}

QString LibComp::spice_netlist(spicecompat::SpiceDialect dialect /* = spicecompat::SPICEDefault */)
{
    Q_UNUSED(dialect);

    QString s = SpiceModel + Name;
#ifdef QUCS_ENABLE_CORE
    // CORE hierarchical schematics already expose Gnd as a port — do not add library gnd.
    QString coreSch;
    QString hint;
    if (containingSchematic) {
      hint = containingSchematic->getDocName();
    }
    bool coreHier = Props.size() >= 2
        && qucs_core::tryResolveCoreSchematic(Props.at(0)->Value, Props.at(1)->Value, coreSch, hint);
    if (!coreHier && Props.size() >= 2 && !hint.trimmed().isEmpty()) {
      QDir parent = QFileInfo(hint).absoluteDir();
      if (parent.cdUp()) {
        const QString candidate = parent.filePath(Props.at(1)->Value + QLatin1Char('/')
                                                 + Props.at(1)->Value
                                                 + QStringLiteral(".schematic.core"));
        coreHier = QFileInfo::exists(candidate);
      }
    }
    if (!coreHier)
#endif
    {
        s += QStringLiteral(" 0"); // connect ground of traditional .lib subckt to circuit ground
    }
    for (Port *p1 : std::as_const(Ports))
      s += " "  + spicecompat::normalize_node_name(p1->Connection->Name);   // node names
    s += " " + createType();

    // output user defined parameters
    for(int i = 2;i<Props.size();i++) {
      QString val = spicecompat::normalize_value(Props.at(i)->Value);
      s += " "+Props.at(i)->Name+"="+val;
    }
    s +="\n";

    return s;
}

QString LibComp::cdl_netlist()
{
    return spice_netlist(spicecompat::CDL);
}

QString LibComp::getSpiceLibrary()
{
  QStringList files;
  QString content;
  QStringList includes,attach;

  int r = loadSection("Spice",content,&includes,&attach);
  if (r<0) {
    return QString();
  }
  for (const auto &file : std::as_const(attach)) {
    if (file.endsWith(".cir", Qt::CaseInsensitive) ||
        file.endsWith(".ckt", Qt::CaseInsensitive) ||
        file.endsWith(".lib", Qt::CaseInsensitive) ||
        file.endsWith(".sp", Qt::CaseInsensitive)) {
      files.append(getSubcircuitFile()+'/'+file);
    }
  }

  QString s;
  for (const auto &file: files) { // for netlist
    s += QStringLiteral(".INCLUDE \"%1\"\n").arg(file);
  }
  return s;
}
