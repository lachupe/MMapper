#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "../parser/LoginLines.h"

#include <optional>

#include <QString>

class GmcpMessage;

/// MMapper.Session.RememberLogin {"remember": true|false}: the driving frontend asks MMapper to
/// remember the account it is logging in to as MMapper's own auto-login (Preferences > General >
/// "auto login": config.account.accountName, account.rememberLogin, the pass phrase in the
/// system keychain through PasswordConfig), so that the next connection logs in by itself
/// (GMCP Char.Login, Proxy's virt_onTryCharLogin()).
///
/// The request carries only the flag. What is remembered is what MMapper itself relayed to MUME
/// while MUME waited at its own login prompts (LoginLinesTracker): the line sent at the name
/// prompt is the account, the line sent at the pass phrase prompt after it is the pass phrase.
/// Both are held here, in memory, until MUME accepts them -- its account menu, or a character in
/// the game -- and then handed once to the keychain and forgotten. They are dropped when the
/// frontend withdraws the request, when MUME goes, and when the session is released.
///
/// The pass phrase never leaves this object except through takeAccepted(), whose one caller
/// gives it to PasswordConfig: it is in no package, no log and no replay cache.
///
/// Without QtKeychain (a build with WITH_QTKEYCHAIN=OFF) nothing can be stored safely, and the
/// request is refused: unavailableReason() says why, and MMapper.Session.State carries it as
/// `rememberUnavailable`. Nothing is ever written to the plain configuration but the account
/// name and the two flags.
class NODISCARD LoginMemory final
{
public:
    struct NODISCARD Credentials final
    {
        QString account;
        QString passPhrase;
    };

    /// MMapper.Session.State's `rememberUnavailable` in a build without a keychain.
    static constexpr const char *const NO_KEYCHAIN = "no keychain";

private:
    bool m_keychain = false;
    bool m_requested = false;
    QString m_account;
    QString m_passPhrase;
    /// Why remembering failed or cannot be done: NO_KEYCHAIN, or the keychain's own error; empty
    /// while it can.
    QString m_unavailable;

public:
    explicit LoginMemory(bool keychainAvailable);

public:
    NODISCARD bool requested() const { return m_requested; }
    NODISCARD bool keychainAvailable() const { return m_keychain; }
    NODISCARD const QString &unavailableReason() const { return m_unavailable; }

    /// The frontend's request. Remembering on is refused without a keychain (false returned);
    /// off is always taken, and drops whatever was held.
    NODISCARD bool setRequested(bool remember);

    /// A line the driving frontend sent while MUME waited at `prompt` (MMapper.Session.State's
    /// `login`). Kept only while remembering was asked for, and only at a login prompt.
    void noteSent(LoginPromptKindEnum prompt, const QString &line);

    /// MUME accepted the login (its account menu, or a character in the game): the account and
    /// pass phrase to store, once, if remembering was asked for and both were relayed; both are
    /// forgotten here either way, and the request is spent.
    NODISCARD std::optional<Credentials> takeAccepted();

    /// MUME went, or the session was released: everything held is dropped, the request too.
    void reset();

    /// The keychain refused or failed: said in the state until remembering works again.
    void setUnavailable(const QString &reason) { m_unavailable = reason; }
    void clearUnavailable();
};

/// The flag of an MMapper.Session.RememberLogin, or nothing when the payload is not an object
/// with a boolean `remember`.
NODISCARD std::optional<bool> parseRememberLogin(const GmcpMessage &msg);

/// MMapper.Session.State's `rememberedAccount`: the account MMapper logs in to by itself, from
/// its configuration, or an empty string (sent as null) when it remembers none. Never the pass
/// phrase, which only the keychain holds.
NODISCARD QString rememberedAccountName(const QString &accountName,
                                        bool passwordStored,
                                        bool rememberLogin,
                                        bool keychainAvailable);
