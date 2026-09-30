// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "TestTradeLines.h"

#include "../src/frontend/TradeMessages.h"
#include "../src/observer/gameobserver.h"
#include "../src/parser/TradeLines.h"

#include <QtTest/QtTest>

void TestTradeLines::interfaceTest()
{
    const GmcpMessage msg = frontend_messages::makeShopList(ShopList{});
    QCOMPARE(msg.getType(), GmcpMessageTypeEnum::MMAPPER_SHOP_LIST);
}

QTEST_MAIN(TestTradeLines)
