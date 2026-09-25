#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestItemLines final : public QObject
{
    Q_OBJECT

public:
    TestItemLines() = default;
    ~TestItemLines() override = default;

private Q_SLOTS:
    static void slotTest();
    static void listedItemTest();
    static void equipmentLineTest();
    static void equipmentBlockTest();
    static void othersEquipmentTest();
    static void peekTest();
    static void inventoryBlockTest();
    static void containerBlockTest();
    static void closedContainerTest();
    static void interruptedTest();
    static void runawayTest();
    static void itemEventTest();
    static void refusedTest();
    static void notItemEventTest();
};
