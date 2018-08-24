#pragma once
/************************************************************************
**
** Authors:   Ulf Hermann <ulfonk_mennhar@gmx.de> (Alve),
**            Marek Krejza <krejza@gmail.com> (Caligor)
**
** This file is part of the MMapper project.
** Maintained by Nils Schimmelmann <nschimme@gmail.com>
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the:
** Free Software Foundation, Inc.
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
************************************************************************/

#ifndef _PATTERNS_H_
#define _PATTERNS_H_

#include <QRegExp>
#include <QStringList>
#include <QtCore>

#include "../configuration/configuration.h"

class QByteArray;
class QRegExp;
class QString;
class QStringList;

class Patterns
{
private:
    // If these were moved to .cpp file, then we wouldn't need to include headers.
    static const Configuration::ParserSettings &parserConfig;
    static QRegExp g_rx;
    static const QStringList g_dynamicDescriptionPatternsList;
    static const QRegExp g_score;

public:
    static bool matchScore(const QString &str);
    static bool matchMoveForcePatterns(const QString &);
    static bool matchNoDescriptionPatterns(const QString &);
    static bool matchPasswordPatterns(const QByteArray &);
    static bool matchPromptPatterns(const QByteArray &);
    static bool matchLoginPatterns(const QByteArray &);
    static bool matchMenuPromptPatterns(const QByteArray &);

    static bool matchPattern(QString pattern, const QString &str);
    static bool matchPattern(QByteArray pattern, const QByteArray &str);
};

#endif
