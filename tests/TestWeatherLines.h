#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestWeatherLines final : public QObject
{
    Q_OBJECT

public:
    TestWeatherLines() = default;
    ~TestWeatherLines() override = default;

private Q_SLOTS:
    static void groundLinesTest();
    static void fogTest();
    static void stormTest();
    static void lightningAndThunderTest();
    static void magicTest();
    static void darknessTest();
    static void unknownTest();
    static void elementTest();
    static void snowInTerrainTest();
    static void frostAfterExitsTest();
    static void unseenRoomTest();
    static void changeWhereStandingTest();
    static void resetTest();
};
