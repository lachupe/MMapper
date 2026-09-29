#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QByteArray>
#include <QObject>

class Map;

class NODISCARD_QOBJECT TestMapStorage final : public QObject
{
    Q_OBJECT

public:
    TestMapStorage() = default;
    ~TestMapStorage() override = default;

private:
    /// `map` as XmlMapStorage exports it (XmlMapStorage befriends this class for it).
    NODISCARD static QByteArray exportXml(const Map &map);

private Q_SLOTS:
    static void fingerprintVectorTest();
    static void fingerprintUtf8VectorTest();
    static void fingerprintOfRoomTest();
    static void xmlExportFingerprintTest();
    static void xmlRoundTripTest();
};
