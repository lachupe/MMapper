// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendLoginMemory.h"

#include "../proxy/GmcpMessage.h"

#include <QJsonDocument>
#include <QJsonObject>

LoginMemory::LoginMemory(const bool keychainAvailable)
    : m_keychain{keychainAvailable}
{
    if (!m_keychain) {
        m_unavailable = QString::fromLatin1(NO_KEYCHAIN);
    }
}

bool LoginMemory::setRequested(const bool remember)
{
    if (!remember) {
        m_requested = false;
        m_account.clear();
        m_passPhrase.clear();
        return true;
    }
    if (!m_keychain) {
        m_requested = false;
        return false;
    }
    m_requested = true;
    return true;
}

void LoginMemory::noteSent(const LoginPromptKindEnum prompt, const QString &line)
{
    if (!m_requested) {
        return;
    }
    switch (prompt) {
    case LoginPromptKindEnum::NONE:
        return;
    case LoginPromptKindEnum::NAME:
        // A name asked for anew (also after "No character or account by that name."): the pass
        // phrase that went with the last one is no longer its.
        m_account = line.trimmed();
        m_passPhrase.clear();
        return;
    case LoginPromptKindEnum::PASSWORD: {
        if (m_account.isEmpty()) {
            return;
        }
        // The line as relayed, less the line end; a pass phrase may begin or end with a space.
        QString phrase = line;
        while (phrase.endsWith(QLatin1Char('\n')) || phrase.endsWith(QLatin1Char('\r'))) {
            phrase.chop(1);
        }
        // After "Wrong password." the next one replaces it.
        m_passPhrase = phrase;
        return;
    }
    }
}

std::optional<LoginMemory::Credentials> LoginMemory::takeAccepted()
{
    std::optional<Credentials> taken;
    if (m_requested && !m_account.isEmpty() && !m_passPhrase.isEmpty()) {
        taken = Credentials{m_account, m_passPhrase};
        m_requested = false;
    }
    m_passPhrase.clear();
    if (taken.has_value()) {
        m_account.clear();
    }
    return taken;
}

void LoginMemory::reset()
{
    m_requested = false;
    m_account.clear();
    m_passPhrase.clear();
}

void LoginMemory::clearUnavailable()
{
    m_unavailable = m_keychain ? QString{} : QString::fromLatin1(NO_KEYCHAIN);
}

std::optional<bool> parseRememberLogin(const GmcpMessage &msg)
{
    const auto &optJson = msg.getJson();
    if (!optJson.has_value()) {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(optJson->toQByteArray());
    if (!doc.isObject()) {
        return std::nullopt;
    }
    const QJsonValue flag = doc.object().value("remember");
    if (!flag.isBool()) {
        return std::nullopt;
    }
    return flag.toBool();
}

QString rememberedAccountName(const QString &accountName,
                              const bool passwordStored,
                              const bool rememberLogin,
                              const bool keychainAvailable)
{
    if (!keychainAvailable || !rememberLogin || !passwordStored) {
        return QString{};
    }
    return accountName.trimmed();
}
