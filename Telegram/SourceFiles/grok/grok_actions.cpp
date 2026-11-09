/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "grok/grok_actions.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "data/data_changes.h"
#include "data/data_chat_filters.h"
#include "data/data_chat_participant_status.h"
#include "data/data_drafts.h"
#include "data/data_folder.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "dialogs/dialogs_main_list.h"
#include "grok/grok_badge.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>

namespace Grok {
namespace {

constexpr auto kReadLimit = 40;
constexpr auto kListLimit = 80;

struct Found {
	History *history = nullptr;
	QString error;
};

[[nodiscard]] QString Kind(not_null<PeerData*> peer) {
	if (peer->isBroadcast()) {
		return u"频道"_q;
	}
	if (peer->isMegagroup() || peer->isChat()) {
		return u"群组"_q;
	}
	return u"私聊"_q;
}

[[nodiscard]] QString Clip(QString text, int limit) {
	text.replace('\n', ' ');
	text.replace('\r', ' ');
	if (text.size() > limit) {
		text = text.left(limit) + u"…"_q;
	}
	return text;
}

void Collect(
		Main::Session &session,
		std::vector<not_null<History*>> &out) {
	const auto addList = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : *list->indexed()) {
			const auto history = row->history();
			if (!history) {
				continue;
			}
			const auto listed = [&] {
				for (const auto existing : out) {
					if (existing.get() == history) {
						return true;
					}
				}
				return false;
			}();
			if (!listed) {
				out.push_back(not_null<History*>(history));
			}
		}
	};
	addList(session.data().chatsList());
	if (const auto archived = session.data().folderLoaded(Data::Folder::kId)) {
		addList(session.data().chatsList(archived));
	}
}

[[nodiscard]] int Score(not_null<PeerData*> peer, const QString &needle) {
	const auto name = peer->name().trimmed();
	const auto lowered = name.toLower();
	const auto username = peer->username().toLower();
	if (!name.isEmpty() && lowered == needle) {
		return 100;
	}
	if (!username.isEmpty() && username == needle) {
		return 95;
	}
	if (!needle.isEmpty() && lowered.contains(needle)) {
		return 80;
	}
	if (!needle.isEmpty() && !username.isEmpty() && username.contains(needle)) {
		return 70;
	}
	return 0;
}

[[nodiscard]] Found FindChat(
		not_null<Window::SessionController*> controller,
		const QString &query,
		bool allowCurrent) {
	const auto trimmed = query.trimmed();
	auto needle = trimmed;
	if (needle.startsWith('@')) {
		needle = needle.mid(1);
	}
	needle = needle.toLower();
	const auto active = controller->activeChatCurrent().history();
	if (needle.isEmpty()) {
		if (allowCurrent && active) {
			return { active };
		}
		return {
			nullptr,
			u"没有指定对话。请说出对话名称，或打开「读取当前聊天」。"_q,
		};
	}
	std::vector<not_null<History*>> histories;
	Collect(controller->session(), histories);
	History *best = nullptr;
	History *second = nullptr;
	auto bestScore = 0;
	auto secondScore = 0;
	for (const auto history : histories) {
		auto score = Score(history->peer, needle);
		if (active && history.get() == active && score > 0) {
			score += 5;
		}
		if (score > bestScore) {
			second = best;
			secondScore = bestScore;
			best = history.get();
			bestScore = score;
		} else if (score > secondScore) {
			second = history.get();
			secondScore = score;
		}
	}
	if (!best || bestScore < 70) {
		return { nullptr, u"没有找到名为「%1」的对话。"_q.arg(trimmed) };
	}
	if (second && secondScore == bestScore) {
		return {
			nullptr,
			u"找到多个同名对话：%1、%2。请说得更具体。"_q.arg(
				best->peer->name(),
				second->peer->name()),
		};
	}
	return { best };
}

[[nodiscard]] QString FormatMessages(
		not_null<History*> history,
		int limit) {
	std::vector<not_null<HistoryItem*>> items;
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			items.push_back(view->data());
		}
	}
	if (int(items.size()) > limit) {
		items.erase(items.begin(), items.end() - limit);
	}
	QStringList lines;
	for (const auto &item : items) {
		auto text = item->originalText().text;
		if (text.trimmed().isEmpty()) {
			text = item->isService() ? u"[服务消息]"_q : u"[附件]"_q;
		}
		const auto sender = item->out()
			? u"我"_q
			: item->from()->name();
		lines.push_back(u"[#%1] %2 %3: %4"_q
			.arg(QString::number(item->fullId().msg.bare))
			.arg(QDateTime::fromSecsSinceEpoch(item->date()).toString(
				u"yyyy-MM-dd hh:mm"_q))
			.arg(sender)
			.arg(Clip(text, 400)));
	}
	auto joined = lines.join('\n');
	if (joined.size() > 10000) {
		joined = joined.right(10000);
	}
	if (lines.isEmpty()) {
		return u"对话「%1」里没有已加载的文字消息。"_q.arg(history->peer->name());
	}
	return u"对话「%1」最近 %2 条：\n%3"_q
		.arg(history->peer->name())
		.arg(lines.size())
		.arg(joined);
}

[[nodiscard]] bool Usable(not_null<History*> history, QString *error) {
	if (history->peer->isForum()) {
		*error = u"「%1」是论坛，这一版还不能直接往话题里写。可以先打开它。"_q
			.arg(history->peer->name());
		return false;
	}
	if (!Data::CanSendTexts(history->peer)) {
		*error = u"没有权限在「%1」里发消息。"_q.arg(history->peer->name());
		return false;
	}
	return true;
}

void Finish(not_null<QObject*> guard, Fn<void(ToolResult)> done, ToolResult result) {
	crl::on_main(guard, [=] {
		done(result);
	});
}

void ReadMessages(
		not_null<Window::SessionController*> controller,
		not_null<QObject*> guard,
		not_null<History*> history,
		int limit,
		Fn<void(ToolResult)> done) {
	const auto itemsNow = [&] {
		auto count = 0;
		for (const auto &block : history->blocks) {
			count += int(block->messages.size());
		}
		return count;
	};
	const auto ready = history->loadedAtBottom()
		&& (itemsNow() >= limit || history->loadedAtTop());
	if (ready) {
		const auto text = FormatMessages(history, limit);
		Finish(guard, std::move(done), {
			text,
			u"读取了消息"_q,
			true,
		});
		return;
	}
	const auto peer = history->peer;
	auto &session = controller->session();
	const auto requestId = session.api().request(MTPmessages_GetHistory(
		peer->input(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(limit),
		MTP_int(0),
		MTP_int(0),
		MTP_long(0)
	)).done(crl::guard(guard, [=](const MTPmessages_Messages &result) {
		session.data().processExistingMessages(peer->asChannel(), result);
		const auto text = FormatMessages(history, limit);
		done(ToolResult{ text, u"读取了消息"_q, true });
	})).fail(crl::guard(guard, [=](const MTP::Error &error) {
		const auto fallback = FormatMessages(history, limit);
		done(ToolResult{
			fallback + u"\n（服务器刷新失败：%1）"_q.arg(error.type()),
			u"读取了本地消息"_q,
			true,
		});
	})).send();
	if (!requestId) {
		Finish(guard, std::move(done), {
			u"读取请求没有发出去。"_q,
			u"读取失败"_q,
			false,
		});
	}
}

} // namespace

bool IsWriteTool(const QString &name) {
	return name == u"send_message"_q || name == u"forward_message"_q;
}

QString DescribeTool(const QString &name, const QJsonObject &args) {
	const auto chat = args.value(u"chat"_q).toString().trimmed();
	const auto to = args.value(u"to_chat"_q).toString().trimmed();
	if (name == u"list_folders"_q) {
		return u"读取分组"_q;
	} else if (name == u"list_chats"_q) {
		return u"查找对话"_q;
	} else if (name == u"read_messages"_q) {
		return chat.isEmpty()
			? u"读取当前聊天"_q
			: u"读取「%1」"_q.arg(chat);
	} else if (name == u"open_chat"_q) {
		return chat.isEmpty()
			? u"打开当前聊天"_q
			: u"打开「%1」"_q.arg(chat);
	} else if (name == u"draft_message"_q) {
		return u"起草到「%1」"_q.arg(chat.isEmpty() ? u"当前聊天"_q : chat);
	} else if (name == u"send_message"_q) {
		return u"发送到「%1」"_q.arg(chat.isEmpty() ? u"当前聊天"_q : chat);
	} else if (name == u"forward_message"_q) {
		return u"转发到「%1」"_q.arg(to.isEmpty() ? u"目标对话"_q : to);
	}
	return name;
}

void RunTool(
		not_null<Window::SessionController*> controller,
		not_null<QObject*> guard,
		bool allowCurrent,
		const QString &name,
		const QJsonObject &args,
		Fn<void(ToolResult)> done) {
	auto &session = controller->session();
	const auto finish = [=](ToolResult result) {
		Finish(guard, done, std::move(result));
	};
	if (name == u"list_folders"_q) {
		QStringList lines;
		lines.push_back(u"- 全部聊天"_q);
		lines.push_back(u"- 归档"_q);
		for (const auto &filter : session.data().chatsFilters().list()) {
			const auto title = filter.titleText().text.trimmed();
			if (!title.isEmpty()) {
				lines.push_back(u"- %1"_q.arg(title));
			}
		}
		finish({
			lines.join('\n'),
			u"读取了 %1 个分组"_q.arg(lines.size()),
			true,
		});
		return;
	}
	if (name == u"list_chats"_q) {
		auto limit = args.value(u"limit"_q).toInt(30);
		limit = std::clamp(limit, 1, kListLimit);
		const auto folderQuery = args.value(u"folder"_q).toString().trimmed();
		const auto query = args.value(u"query"_q).toString().trimmed().toLower();
		const auto &filters = session.data().chatsFilters().list();
		auto filterIndex = -1;
		for (auto i = 0; i < int(filters.size()); ++i) {
			if (filters[i].titleText().text.contains(
					folderQuery,
					Qt::CaseInsensitive)) {
				filterIndex = i;
				break;
			}
		}
		const auto archivedOnly = folderQuery.contains(u"归档"_q);
		const auto allChats = folderQuery.isEmpty()
			|| folderQuery.contains(u"全部"_q);
		if (!allChats && filterIndex < 0 && !archivedOnly) {
			finish({
				u"没有名为「%1」的分组。"_q.arg(folderQuery),
				u"没有这个分组"_q,
				false,
			});
			return;
		}
		std::vector<not_null<History*>> histories;
		Collect(session, histories);
		QStringList lines;
		for (const auto history : histories) {
			if (int(lines.size()) >= limit) {
				break;
			}
			if (archivedOnly) {
				const auto folder = history->folder();
				if (!folder || folder->id() != Data::Folder::kId) {
					continue;
				}
			}
			if (!allChats && !archivedOnly
				&& filterIndex >= 0
				&& !filters[filterIndex].contains(history)) {
				continue;
			}
			if (!query.isEmpty() && Score(history->peer, query) < 70) {
				continue;
			}
			const auto username = history->peer->username();
			lines.push_back(u"- %1%2（%3，未读 %4）"_q
				.arg(history->peer->name())
				.arg(username.isEmpty() ? QString() : u" @"_q + username)
				.arg(Kind(history->peer))
				.arg(history->unreadCount()));
		}
		if (lines.isEmpty()) {
			finish({ u"没有符合条件的对话。"_q, u"没有找到对话"_q, true });
			return;
		}
		finish({
			u"共 %1 个对话：\n%2"_q.arg(lines.size()).arg(lines.join('\n')),
			u"找到 %1 个对话"_q.arg(lines.size()),
			true,
		});
		return;
	}
	if (name == u"read_messages"_q
		|| name == u"open_chat"_q
		|| name == u"draft_message"_q
		|| name == u"send_message"_q) {
		const auto found = FindChat(
			controller,
			args.value(u"chat"_q).toString(),
			allowCurrent);
		if (!found.history) {
			finish({ found.error, found.error, false });
			return;
		}
		const auto history = not_null<History*>(found.history);
		SetOperatingHistory(&session, history);
		if (name == u"read_messages"_q) {
			auto limit = args.value(u"limit"_q).toInt(20);
			limit = std::clamp(limit, 1, kReadLimit);
			ReadMessages(controller, guard, history, limit, std::move(done));
			return;
		}
		if (name == u"open_chat"_q) {
			controller->showPeerHistory(history);
			finish({
				u"已打开「%1」。"_q.arg(history->peer->name()),
				u"已打开「%1」"_q.arg(history->peer->name()),
				true,
			});
			return;
		}
		auto error = QString();
		if (!Usable(history, &error)) {
			finish({ error, error, false });
			return;
		}
		const auto text = args.value(u"text"_q).toString().trimmed();
		if (text.isEmpty()) {
			finish({ u"没有要写入的文字。"_q, u"没有文字"_q, false });
			return;
		}
		if (name == u"draft_message"_q) {
			auto textWithTags = TextWithTags();
			textWithTags.text = text;
			const auto cursor = MessageCursor{
				int(text.size()),
				int(text.size()),
				Ui::kQFixedMax,
			};
			history->setLocalDraft(std::make_unique<Data::Draft>(
				textWithTags,
				FullReplyTo(),
				SuggestOptions(),
				cursor,
				Data::WebPageDraft()));
			session.changes().entryUpdated(
				history,
				Data::EntryUpdate::Flag::LocalDraftSet);
			finish({
				u"已把草稿放进「%1」的输入框，还没有发送。"_q.arg(
					history->peer->name()),
				u"已写入「%1」的草稿"_q.arg(history->peer->name()),
				true,
			});
			return;
		}
		auto action = Api::SendAction(history);
		action.clearDraft = false;
		auto message = Api::MessageToSend(std::move(action));
		message.textWithTags.text = text;
		session.api().sendMessage(std::move(message));
		finish({
			u"已提交发送到「%1」。内容：%2"_q
				.arg(history->peer->name(), Clip(text, 120)),
			u"已提交发送到「%1」"_q.arg(history->peer->name()),
			true,
		});
		return;
	}
	if (name == u"forward_message"_q) {
		const auto from = FindChat(
			controller,
			args.value(u"from_chat"_q).toString(),
			allowCurrent);
		if (!from.history) {
			finish({ from.error, from.error, false });
			return;
		}
		const auto to = FindChat(
			controller,
			args.value(u"to_chat"_q).toString(),
			false);
		if (!to.history) {
			finish({ to.error, to.error, false });
			return;
		}
		const auto fromHistory = not_null<History*>(from.history);
		const auto toHistory = not_null<History*>(to.history);
		auto error = QString();
		if (!Usable(toHistory, &error)) {
			finish({ error, error, false });
			return;
		}
		const auto idText = args.value(u"message_id"_q).isString()
			? args.value(u"message_id"_q).toString()
			: QString::number(args.value(u"message_id"_q).toInteger());
		const auto msgId = MsgId(idText.toLongLong());
		if (!msgId) {
			finish({
				u"缺少 message_id。请先 read_messages，使用结果里的 #编号。"_q,
				u"缺少消息编号"_q,
				false,
			});
			return;
		}
		const auto item = session.data().message(fromHistory->peer, msgId);
		if (!item) {
			finish({
				u"找不到这条消息，请先读取该对话。"_q,
				u"找不到消息"_q,
				false,
			});
			return;
		}
		auto draft = Data::ForwardDraft();
		draft.ids.push_back(item->fullId());
		auto resolved = toHistory->resolveForwardDraft(draft);
		if (resolved.items.empty()) {
			finish({
				u"这条消息不能转发。"_q,
				u"不能转发"_q,
				false,
			});
			return;
		}
		SetOperatingHistory(&session, toHistory);
		auto action = Api::SendAction(toHistory);
		action.clearDraft = false;
		session.api().forwardMessages(
			std::move(resolved),
			action,
			[] {});
		finish({
			u"已提交把消息 #%1 从「%2」转发到「%3」。"_q
				.arg(idText)
				.arg(fromHistory->peer->name())
				.arg(toHistory->peer->name()),
			u"已提交转发到「%1」"_q.arg(toHistory->peer->name()),
			true,
		});
		return;
	}
	finish({
		u"没有名为 %1 的工具。"_q.arg(name),
		u"未知工具"_q,
		false,
	});
}

} // namespace Grok
