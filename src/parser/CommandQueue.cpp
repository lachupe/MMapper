// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2019 The MMapper Authors
// Author: Nils Schimmelmann <nschimme@gmail.com> (Jahara)

#include "CommandQueue.h"

#include "../mapdata/ExitDirection.h"

QByteArray CommandQueue::toByteArray() const
{
    QByteArray dirs;
    for (int i = 0; i < base::size(); i++) {
        const auto cmd = base::at(i);
        // REVISIT: Serialize/deserialize directions more intelligently
        dirs.append(Mmapper2Exit::charForDir(static_cast<ExitDirection>(cmd)));
    }
    return dirs;
}

CommandQueue &CommandQueue::operator=(const QByteArray &dirs)
{
    base::clear();
    for (int i = 0; i < dirs.length(); i++) {
        base::enqueue(static_cast<CommandIdType>(Mmapper2Exit::dirForChar(dirs.at(i))));
    }
    return *this;
}
