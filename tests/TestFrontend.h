#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestFrontend final : public QObject
{
    Q_OBJECT

public:
    TestFrontend() = default;
    ~TestFrontend() override = default;

private Q_SLOTS:
    static void subscriptionSetTest();
    static void subscriptionAddRemoveTest();
    static void subscriptionSplitModuleTest();
    static void subscriptionMalformedTest();
    static void relayFilterTest();
    static void terminalOutputTest();
    static void sessionStateTest();
    static void inputSubscriptionTest();
    static void errorTest();
    static void mapPositionTest();
    static void mapPositionWithoutServerIdTest();
    static void xmlElementTest();
    static void combatEventTest();
    static void timeStateTest();
    static void weatherEventTest();
    static void groundStateTest();
    static void roomContentsTest();
    static void containerEventTest();
    static void charEquipmentTest();
    static void charInventoryTest();
    static void charContainerTest();
    static void charItemTest();
    static void replayChangedFieldsTest();
    static void replaySetChangesTest();
    static void mumeModuleCoverageTest();
    static void mumeMessageCoverageTest();
};
