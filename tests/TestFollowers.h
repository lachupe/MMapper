#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestFollowers final : public QObject
{
    Q_OBJECT

public:
    TestFollowers() = default;
    ~TestFollowers() override = default;

private Q_SLOTS:
    static void linesTest();
    static void notFollowerLinesTest();
    static void commandsTest();
    static void namesTest();
    static void sequenceTest();
    static void repliesTest();
    static void pairingTest();
    static void playersTest();
    static void mountTest();
    static void labelsTest();
    static void ownDeathTest();
    static void limitsTest();
    static void resetTest();
    static void followingTest();
    static void leaderTest();
    static void protectTest();
};
