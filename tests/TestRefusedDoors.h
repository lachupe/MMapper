#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestRefusedDoors final : public QObject
{
    Q_OBJECT

public:
    TestRefusedDoors() = default;
    ~TestRefusedDoors() override = default;

private Q_SLOTS:
    // MMapper.Char.Refused
    static void refusedLinesTest();
    static void notRefusedTest();
    static void refusedDoorReplyTest();
    static void reasonsTest();
    // MMapper.Room.Door
    static void doorLinesTest();
    static void notDoorLinesTest();
    static void exitsLineTest();
    static void gmcpExitsTest();
    static void aimTest();
    static void roomInfoTest();
    static void repliesTest();
    static void backroomIceTest();
    static void iceMoundTest();
    static void rockTest();
    static void blockTest();
    static void bashAndBreakTest();
    static void movesAndLooksTest();
    static void othersTest();
    static void hiddenNamesTest();
    static void roomChangeTest();
};
