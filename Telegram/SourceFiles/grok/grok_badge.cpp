/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "grok/grok_badge.h"

#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "styles/style_dialogs.h"
#include "ui/painter.h"

namespace Grok {
namespace {

[[nodiscard]] PeerId &Operating() {
	static auto peerId = PeerId();
	return peerId;
}

} // namespace

PeerId OperatingPeer() {
	return Operating();
}

void SetOperatingPeer(PeerId peerId) {
	Operating() = peerId;
}

void SetOperatingHistory(Main::Session *session, History *history) {
	if (!session) {
		return;
	}
	const auto now = history ? history->peer->id : PeerId();
	const auto was = Operating();
	if (was == now) {
		return;
	}
	Operating() = now;
	if (was) {
		if (const auto peer = session->data().peerLoaded(was)) {
			session->data().history(peer)->updateChatListEntry();
		}
	}
	if (history) {
		history->updateChatListEntry();
	}
}

void PaintOperatingBadge(Painter &p, PeerId peerId, const QRect &row) {
	if (!peerId || peerId != Operating()) {
		return;
	}
	p.save();
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0x1D, 0x9B, 0xF0));
	p.drawRoundedRect(
		QRect(row.left(), row.top(), st::lineWidth * 3, row.height()),
		st::lineWidth,
		st::lineWidth);

	const auto text = u"Grok 操作中"_q;
	const auto font = st::dialogsTextFont;
	p.setFont(font);
	const auto padX = font->height / 2;
	const auto badgeW = font->width(text) + padX * 2;
	const auto badgeH = font->height + st::lineWidth * 2;
	const auto badge = QRect(
		row.right() - badgeW - st::lineWidth * 4,
		row.top() + st::lineWidth * 3,
		badgeW,
		badgeH);
	p.drawRoundedRect(badge, badgeH / 2., badgeH / 2.);
	p.setPen(Qt::white);
	p.drawText(badge, Qt::AlignCenter, text);
	p.restore();
}

} // namespace Grok
