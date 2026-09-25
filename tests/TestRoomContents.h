#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestRoomContents final : public QObject
{
    Q_OBJECT

public:
    TestRoomContents() = default;
    ~TestRoomContents() override = default;

private Q_SLOTS:
    static void keywordTest();
    static void namesContainerTest();
    static void objectsTest();
    static void charactersLeftOutTest();
    static void terrainLeftOutTest();
    static void unseenRoomTest();
    static void onlyAtPromptTest();
    static void commandTest();
    static void itemTest();
    static void openTest();
    static void lockedTest();
    static void unlockTest();
    static void pickTest();
    static void lookInTest();
    static void getTest();
    static void doorRepliesTest();
    static void notFoundTest();
    static void stateRidesAlongTest();
    static void expiryTest();
};
