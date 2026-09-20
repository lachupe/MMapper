// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "FrontendSession.h"

#include "../global/utils.h"
#include "../proxy/TextCodec.h"
#include "../proxy/connectionlistener.h"

#include <memory>
#include <tuple>

#include <QByteArray>

FrontendSession::FrontendSession()
    // The same terminal type the built-in client reports. It is relayed to MUME, and
    // announcing an unfamiliar client there could change what the game sends back.
    : AbstractTelnet(TextCodecStrategyEnum::FORCE_UTF_8, TelnetTermTypeBytes{"MMapper"})
{
    QObject::connect(&m_socket, &QIODevice::readyRead, &m_dummy, [this]() { onReadyRead(); });
}

FrontendSession::~FrontendSession()
{
    m_socket.disconnectFromHost();
}

bool FrontendSession::attach(ConnectionListener &listener)
{
    if (isAttached()) {
        return true;
    }
    // Checked rather than attempted: startClient() answers a second client by writing a
    // refusal into its socket, which would leave this session half-open.
    if (listener.hasClient()) {
        return false;
    }

    auto peerSocket = std::make_unique<VirtualSocket>();
    m_socket.connectToPeer(peerSocket.get());
    listener.startClient(std::move(peerSocket));
    reset();
    return true;
}

void FrontendSession::detach()
{
    if (isAttached()) {
        m_socket.disconnectFromHost();
    }
}

void FrontendSession::sendLine(const QString &line)
{
    if (!isAttached()) {
        return;
    }
    // The trailing newline is what makes the line filter hand a complete command to the
    // parser; the built-in client appends one at the same point.
    submitOverTelnet(line + QString(char_consts::C_NEWLINE), false);
}

void FrontendSession::virt_sendRawData(const TelnetIacBytes &data)
{
    m_socket.write(data.getQByteArray());
}

void FrontendSession::virt_sendToMapper(const RawBytes & /*data*/, bool /*goAhead*/)
{
    // Discarded on purpose. Terminal output reaches frontends through GameObserver, which
    // observes every session; publishing it here as well would duplicate every line for a
    // frontend that happens to be the one driving.
}

void FrontendSession::onReadyRead()
{
    std::ignore = io::readAllAvailable(m_socket, m_buffer, [this](const QByteArray &byteArray) {
        assert(!byteArray.isEmpty());
        onReadInternal(TelnetIacBytes{byteArray});
    });
}
