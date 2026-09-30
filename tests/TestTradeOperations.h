#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestTradeOperations final : public QObject
{
    Q_OBJECT

public:
    TestTradeOperations() = default;
    ~TestTradeOperations() override = default;

private Q_SLOTS:
    static void operationMessageTest();
    static void shopListTest();
    static void shopBuyTest();
    static void shopSellTest();
    static void guildListTest();
    static void practiseTimesTest();
    static void practiseLimitTest();
    static void practiseRefusalTest();
    static void innOfferAndRentTest();
    static void innRetireTest();
    static void trophiesPagerTest();
    static void tooManyPagesTest();
    static void playerPreemptionTest();
    static void foreignLineTest();
    static void timeoutTest();
    static void refusalsTest();
    static void cancelTest();
    static void abortTest();
    static void viewerClaimTest();
    static void viewerSettingTest();
};
