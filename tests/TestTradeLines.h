#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestTradeLines final : public QObject
{
    Q_OBJECT

public:
    TestTradeLines() = default;
    ~TestTradeLines() override = default;

private Q_SLOTS:
    static void pagerLineTest();
    static void classifyChunkTest();
    static void moneyTest();
    static void shopListTest();
    static void shopListEmptyAndForeignTest();
    static void shopListPagedTest();
    static void shopDealTest();
    static void shopClosedAndNotDealsTest();
    static void guildTeacherTest();
    static void guildTeacherOldFormTest();
    static void guildTeacherPagedTest();
    static void practisedTest();
    static void charSkillsTest();
    static void charSkillsPagedTest();
    static void charSkillsOwnLogTest();
    static void innOfferTest();
    static void trophiesTest();
    static void trophiesPagedTest();
    static void readersOrderTest();
    static void quietCommandAllowedTest();
    static void quietTrafficTest();
    static void quietCaptureTest();
    static void quietReadersTest();
    static void quietReadersForeignTest();
    static void messagesTest();
    static void resetTest();
};
