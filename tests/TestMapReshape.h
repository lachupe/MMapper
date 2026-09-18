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

    // Scoring: basics.
    static void cleanLayoutScoresNearZeroTest();
    static void wrongDirectionIsPenalizedTest();
    static void misalignmentIsProportionalTest();
    static void overlongEdgeIsMildTest();
    static void boundaryStretchIsCheaperTest();
    static void collisionIsDetectedTest();
    static void externalObstacleCollidesTest();
    static void movingImmovableRoomIsRejectedTest();
    static void mismatchedPositionsThrowTest();

    // Scoring: the spec's priority ordering. These check the model, not the
    // implementation -- they are what catches a badly tuned weight table.
    static void stretchingBeatsNewLayerTest();
    static void directionBeatsExactSpacingTest();
    static void distantMarginCostsMoreTest();
    static void coreMovesCheaperThanMarginTest();
    static void collisionOutranksEverythingTest();

    // Solver.
    static void cleanGridIsLeftAloneTest();
    static void misalignedRoomIsStraightenedTest();
    static void wrongDirectionIsFixedTest();
    static void unnecessaryGapIsClosedTest();
    static void immovableRoomsNeverMoveTest();
    static void solverIntroducesNoCollisionsTest();
    static void solvedLayoutIsLocalMinimumTest();
    static void solverNeverWorsensScoreTest();
    static void emptyScopeIsUnchangedTest();
};
