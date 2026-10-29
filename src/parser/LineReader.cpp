// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "LineReader.h"

#include <QStringList>

SentCommand::SentCommand(const QString &sentLine, const EpochMs now)
    : line{sentLine}
    , nowMs{now}
{
    const QString simple = sentLine.simplified();
    const qsizetype space = simple.indexOf(QLatin1Char(' '));
    verb = (space < 0 ? simple : simple.left(space)).toLower();
    args = space < 0 ? QString{} : simple.mid(space + 1);
}

std::vector<SentCommand> splitSentCommands(const QString &sent, const EpochMs now)
{
    QStringList pieces = sent.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (pieces.size() > 1 && pieces.back().isEmpty()) {
        pieces.removeLast();
    }
    std::vector<SentCommand> result;
    result.reserve(static_cast<size_t>(pieces.size()));
    for (QString &piece : pieces) {
        if (piece.endsWith(QLatin1Char('\r'))) {
            piece.chop(1);
        }
        result.emplace_back(piece, now);
    }
    return result;
}

LineReader::~LineReader() = default;

void LineReaders::reset(const ResetReason reason)
{
    for (LineReader *const reader : m_readers) {
        if (reader->resetsOn().has(reason)) {
            reader->reset();
        }
    }
}

void LineReaders::receiveCommand(const SentCommand &command)
{
    for (LineReader *const reader : m_readers) {
        reader->receiveCommand(command);
    }
}
