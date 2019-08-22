#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2019 The MMapper Authors
// Author: Nils Schimmelmann <nschimme@gmail.com> (Jahara)

#include <functional>
#include <QByteArray>
#include <QString>

static constexpr const uint32_t MAX_UNICODE_CODEPOINT = 0x10FFFFu;

class OptQChar final
{
private:
    QChar qc{};
    bool valid = false;

public:
    explicit OptQChar() = default;
    explicit OptQChar(QChar _qc)
        : qc{_qc}
        , valid{true}
    {}
    explicit operator bool() const { return valid; }
    QChar value() const { return qc; }
};

namespace entities {
struct EncodedLatin1;
/// Without entities
struct DecodedUnicode : QString
{
    using QString::QString;
    explicit DecodedUnicode(const char *s)
        : QString{QString::fromLatin1(s)}
    {}
    explicit DecodedUnicode(QString s)
        : QString{std::move(s)}
    {}

    DecodedUnicode(QByteArray) = delete;
    DecodedUnicode(QByteArray &&) = delete;
    DecodedUnicode(const QByteArray &) = delete;
    DecodedUnicode(EncodedLatin1) = delete;
    DecodedUnicode(EncodedLatin1 &&) = delete;
    DecodedUnicode(const EncodedLatin1 &) = delete;
};
/// With entities
struct EncodedLatin1 : QByteArray
{
    using QByteArray::QByteArray;
    explicit EncodedLatin1(const char *s)
        : QByteArray{s}
    {}
    explicit EncodedLatin1(QByteArray s)
        : QByteArray{std::move(s)}
    {}
    EncodedLatin1(QString) = delete;
    EncodedLatin1(QString &&) = delete;
    EncodedLatin1(const QString &) = delete;
    EncodedLatin1(DecodedUnicode) = delete;
    EncodedLatin1(DecodedUnicode &&) = delete;
    EncodedLatin1(const DecodedUnicode &) = delete;
};

enum class EncodingType { Translit, Lossless };
EncodedLatin1 encode(const DecodedUnicode &name, EncodingType encodingType = EncodingType::Translit);
DecodedUnicode decode(const EncodedLatin1 &input);

struct EntityCallback
{
    virtual ~EntityCallback();
    virtual void decodedEntity(int start, int len, OptQChar decoded) = 0;
};
void foreachEntity(const QStringRef &line, EntityCallback &callback);

} // namespace entities
