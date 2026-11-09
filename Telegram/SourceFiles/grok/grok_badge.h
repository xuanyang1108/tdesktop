/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "ui/painter.h"

class History;

namespace Main {
class Session;
} // namespace Main

namespace Grok {

[[nodiscard]] PeerId OperatingPeer();
void SetOperatingPeer(PeerId peerId);
void SetOperatingHistory(Main::Session *session, History *history);
void PaintOperatingBadge(Painter &p, PeerId peerId, const QRect &row);

} // namespace Grok
