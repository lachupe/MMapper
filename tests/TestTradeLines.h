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
    static void interfaceTest();
};
