#pragma once
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 The MMapper Authors

#include "../global/macros.h"
#include "XmlElement.h"

#include <cstddef>
#include <vector>

#include <QString>

/// Reassembles MUME's XML tags into complete elements.
///
/// MumeXmlParser already splits the incoming stream into tag bodies and text; this turns
/// that back into a document. It is fed every tag and every run of text in the order they
/// arrived, and hands back each element once its closing tag shows up, with any nested
/// elements inside it.
///
/// Deliberately free of everything but QString so it can be unit tested on its own, in the
/// same spirit as FrontendSubscriptions. It holds no opinion about what the elements mean:
/// deciding that a HIT is a combat event is the consumer's job.
///
/// MUME's XML is, in its own words, "not very strict", so the failure modes are real and
/// are all handled without losing sync: a closing tag that matches nothing is ignored, a
/// closing tag that matches further down the stack implicitly closes everything above it
/// (each one marked truncated), and an element that runs past the size or nesting limits is
/// truncated rather than allowed to grow without bound.
class NODISCARD XmlElementTracker final
{
public:
    /// Largest text a single element may accumulate. MUME can open an element and, after a
    /// dropped or malformed close, never shut it; this is what stops one element quietly
    /// eating a whole session of output.
    static constexpr const int MAX_TEXT_LENGTH = 8192;
    /// Deepest nesting accepted. Real documents are a few levels deep; past this the input
    /// is malformed rather than something worth allocating for.
    static constexpr const size_t MAX_DEPTH = 16;
    /// Most completed elements held between drains. The parser drains every line, so this
    /// only matters if a consumer stops collecting.
    static constexpr const size_t MAX_PENDING = 256;

private:
    std::vector<XmlElement> m_open;
    std::vector<XmlElement> m_completed;
    /// Opening tags refused because the stack was already at MAX_DEPTH. Counted so that
    /// their closing tags can be swallowed too and the stack stays in step.
    size_t m_suppressed = 0;

public:
    /// Feeds one tag: what stood between < and >, with the angle brackets removed. Handles
    /// opening tags, closing tags ("/room") and self-closing tags ("exit dir=north/").
    void receiveTag(const QString &tagBody);

    /// Feeds a run of text, which becomes part of every element currently open.
    void receiveText(const QString &text);

    /// Elements completed since the last call, in the order they closed. Elements nested
    /// inside another are not returned separately; they are inside their parent.
    NODISCARD std::vector<XmlElement> take();

    /// Drops everything, open and completed alike. For a new session, or when XML mode goes
    /// away and whatever was open will never be closed.
    void reset();

    NODISCARD size_t depth() const { return m_open.size(); }
    NODISCARD bool hasOpenElements() const { return !m_open.empty(); }

private:
    void openElement(const QString &tagBody, const QString &name);
    void closeElement(const QString &name);
    void popTop(bool implicit);
};
