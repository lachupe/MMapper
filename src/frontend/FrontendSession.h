#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/io.h"
#include "../global/macros.h"
#include "../proxy/AbstractTelnet.h"
#include "../proxy/VirtualSocket.h"

#include <QObject>
#include <QString>

class ConnectionListener;

/// Attaches a frontend to MMapper's downstream session so that it can send player input.
///
/// MMapper accepts exactly one downstream client, and the built-in client is not special:
/// it joins through a VirtualSocket pair, and so does this. The frontend therefore becomes
/// an ordinary session peer, and input travels the same path as input from the built-in
/// client -- through UserTelnet and the line filter into AbstractParser -- so mapper
/// commands, movement tracking and logging all behave exactly as they do today.
///
/// Only the input direction is used. Downstream text and GMCP already reach frontends
/// through GameObserver, which sees every session regardless of who owns it, so this class
/// deliberately discards what the proxy sends back rather than publishing it a second time.
class NODISCARD FrontendSession final : private AbstractTelnet
{
private:
    VirtualSocket m_socket;
    QObject m_dummy;
    io::buffer<(1 << 15)> m_buffer;

public:
    explicit FrontendSession();
    ~FrontendSession() final;

public:
    /// Takes the session slot. Returns false if a client already owns it, in which case
    /// nothing is attached and the frontend stays a read-only observer.
    NODISCARD bool attach(ConnectionListener &listener);
    void detach();
    NODISCARD bool isAttached() const { return m_socket.isConnected(); }

public:
    /// Sends one line of player input, exactly as the built-in client's input box does.
    void sendLine(const QString &line);

private:
    void virt_sendRawData(const TelnetIacBytes &data) final;
    void virt_sendToMapper(const RawBytes &data, bool goAhead) final;

private:
    void onReadyRead();
};
