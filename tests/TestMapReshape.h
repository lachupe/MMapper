#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../src/global/macros.h"

#include <QObject>

class NODISCARD_QOBJECT TestMapReshape final : public QObject
{
    Q_OBJECT

public:
    TestMapReshape();
    ~TestMapReshape() final;

private Q_SLOTS:
    static void rolesByGraphDistanceTest();
    static void pinnedOverridesRoleTest();
    static void horizontalEdgesTest();
    static void boundaryEdgesTest();
    static void verticalExitsAreSeparatedTest();
    static void unusableExitsAreRecordedTest();
    static void occupancyIncludesUnconnectedRoomsTest();
    static void buildForAreaTest();
    static void existingCollisionsAreReportedTest();
    static void staleSelectionIsIgnoredTest();
};
