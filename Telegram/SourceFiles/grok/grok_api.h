/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "grok/grok_actions.h"
#include "grok/grok_storage.h"
#include "mtproto/core_types.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QObject>

#include <vector>

namespace Window {
class SessionController;
} // namespace Window

namespace Grok {

enum class StepState {
	Running,
	Waiting,
	Done,
	Failed,
};

struct Step {
	QString text;
	StepState state = StepState::Running;
};

struct Bubble {
	QString role;
	QString text;
	std::vector<Step> steps;
	bool showPause = false;
	bool showConfirm = false;
};

class Agent final : public QObject {
public:
	Agent(
		not_null<Window::SessionController*> controller,
		not_null<Storage*> storage,
		Fn<void(bool stick)> refresh);
	~Agent();

	void send(const QString &text);
	void confirm();
	void pause();
	void newChat();
	void loadActive();

	[[nodiscard]] bool running() const;
	[[nodiscard]] const std::vector<Bubble> &bubbles() const;

private:
	void callModel();
	void processTools();
	void finishTool(
		const QString &id,
		int stepIndex,
		const ToolResult &result);
	void failRun(const QString &text);
	void finishRun();
	void refresh(bool stick);
	void persist();
	[[nodiscard]] QString systemPrompt() const;
	[[nodiscard]] QString contextNote() const;
	[[nodiscard]] QString activeId() const;

	const not_null<Window::SessionController*> _controller;
	const not_null<Storage*> _storage;
	Fn<void(bool)> _refresh;

	std::vector<Bubble> _bubbles;
	QJsonArray _api;
	QJsonArray _toolCalls;
	int _toolIndex = 0;
	int _rounds = 0;
	int _generation = 0;
	bool _running = false;
	bool _waiting = false;
	QString _pendingId;
	QString _pendingName;
	QJsonObject _pendingArgs;
	int _pendingStep = -1;
	mtpRequestId _requestId = 0;
	QPointer<QNetworkReply> _reply;

};

} // namespace Grok
