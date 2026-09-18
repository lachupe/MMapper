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

    // Structural moves: whole-row and whole-column shifts.
    static void structuralShiftEscapesDeadlockTest();
    static void insertedColumnCreatesNoFakeRoomTest();
    static void structuralShiftRespectsImmovableRoomsTest();

    // Applying a solved layout back to the map.
    static void naiveApplyCorruptsTheMapTest();
    static void stagedApplyKeepsMapConsistentTest();
    static void stagedApplyIsOneUndoStepTest();
    static void emptyMoveListProducesNoChangesTest();

    // The `_map area ...` subcommand tree.
    static void areaSubcommandsDispatchTest();
    static void nearSubcommandsDispatchTest();
    static void solveRespectsIterationBudgetTest();

    // Whole-storey moves in z.
    static void volumetricLiftsWholeStoreyTest();
    static void liftingOneRoomAloneIsRejectedTest();
    static void flattenCollapsesStoreyOntoParentTest();
    static void volumetricRebuildsTowerTest();
    static void hangingAreaMovesUnderItsEntranceTest();
    static void draggedApartIslandClosesUpTest();
    static void distantNeighbourDoesNotDominateTest();
    static void scorerAndSolverAgreeOnEdgeCostTest();
    static void volumetricStacksRoomsAboveEachOtherTest();
    static void volumetricKeepsHorizontalExitsOnOneLayerTest();
    static void volumetricIgnoresOriginalHeightTest();

    // Detecting groups that want drawing at a smaller scale.
    static void crampedGroupIsFlaggedForScalingTest();
    static void naturallySparseGroupIsNotFlaggedTest();
    static void suggestedScaleShrinksTheFootprintTest();

    // Z handling: the two modes.
    static void flattenCollapsesUnnecessaryLayerTest();
    static void flattenLeavesHandFlattenedStackAloneTest();
    static void volumetricRestoresVerticalityTest();
    static void volumetricKeepsExistingStackTest();
    static void flattenNeverOpensANewLayerTest();
};
