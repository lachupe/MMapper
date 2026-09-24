// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendSubscriptions.h"

#include "../global/CaseUtils.h"
#include "../global/Consts.h"
#include "../global/TextUtils.h"

#include <exception>
#include <string>

#include <QDebug>

bool FrontendSubscriptions::applySupports(const GmcpMessage &msg)
{
    const bool isSupports = msg.isCoreSupportsSet() || msg.isCoreSupportsAdd()
                            || msg.isCoreSupportsRemove();
    if (!isSupports) {
        return false;
    }

    const auto &optDoc = msg.getJsonDocument();
    if (!optDoc.has_value()) {
        return false;
    }
    const auto optArray = optDoc->getArray();
    if (!optArray.has_value()) {
        return false;
    }

    if (msg.isCoreSupportsSet()) {
        m_modules.clear();
    }

    const bool enabled = !msg.isCoreSupportsRemove();
    for (const auto &element : optArray.value()) {
        const auto optString = element.getString();
        if (!optString.has_value()) {
            continue;
        }
        try {
            set(GmcpModule{mmqt::toStdStringUtf8(optString.value())}, enabled);
        } catch (const std::exception &ex) {
            qWarning() << "[frontend] ignoring module" << optString.value()
                       << "because:" << ex.what();
        }
    }
    return true;
}

void FrontendSubscriptions::set(const GmcpModule &mod, const bool enabled)
{
    if (enabled) {
        m_modules.insert(mod);
    } else {
        m_modules.erase(mod);
    }
}

bool FrontendSubscriptions::isRelayable(const GmcpMessage &msg)
{
    // Package names are case insensitive, as module names are.
    const std::string name = ::toLowerUtf8(msg.getName().getStdStringUtf8());
    return !name.starts_with("core.") && !name.starts_with("mume.client.");
}

bool FrontendSubscriptions::wants(const GmcpMessage &msg) const
{
    const auto name = msg.getName().getStdStringUtf8();
    const std::size_t found = name.find_last_of(char_consts::C_PERIOD);
    if (found == std::string::npos) {
        // A package with no dot has no module to subscribe to.
        return false;
    }
    try {
        return m_modules.find(GmcpModule{name.substr(0, found)}) != m_modules.end();
    } catch (const std::exception &ex) {
        qWarning() << "[frontend] cannot resolve module for" << msg.toRawBytes()
                   << "because:" << ex.what();
        return false;
    }
}
