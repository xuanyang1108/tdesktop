/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "grok/grok_api.h"

#include "grok/grok_badge.h"
#include "history/history.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"

#include <QtCore/QJsonDocument>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Grok {
namespace {

constexpr auto kEndpoint = "https://api.x.ai/v1/chat/completions";
constexpr auto kMaxRounds = 6;

[[nodiscard]] QJsonObject Prop(const QString &type, const QString &description) {
	QJsonObject object;
	object.insert(u"type"_q, type);
	object.insert(u"description"_q, description);
	return object;
}

[[nodiscard]] QJsonObject Tool(
		const QString &name,
		const QString &description,
		const QJsonObject &properties,
		const QJsonArray &required) {
	QJsonObject parameters;
	parameters.insert(u"type"_q, u"object"_q);
	parameters.insert(u"properties"_q, properties);
	if (!required.isEmpty()) {
		parameters.insert(u"required"_q, required);
	}
	QJsonObject function;
	function.insert(u"name"_q, name);
	function.insert(u"description"_q, description);
	function.insert(u"parameters"_q, parameters);
	QJsonObject tool;
	tool.insert(u"type"_q, u"function"_q);
	tool.insert(u"function"_q, function);
	return tool;
}

[[nodiscard]] QJsonArray ToolSpecs() {
	QJsonArray tools;
	tools.append(Tool(
		u"list_folders"_q,
		u"列出对话分组，包括全部聊天和归档。"_q,
		QJsonObject(),
		QJsonArray()));
	tools.append(Tool(
		u"list_chats"_q,
		u"按名称或分组列出对话。query 和 folder 都可以留空。"_q,
		QJsonObject{
			{ u"query"_q, Prop(u"string"_q, u"对话名称或用户名的一部分"_q) },
			{ u"folder"_q, Prop(u"string"_q, u"分组名称，例如工作频道"_q) },
			{ u"limit"_q, Prop(u"integer"_q, u"最多返回多少个，默认 30"_q) },
		},
		QJsonArray()));
	tools.append(Tool(
		u"read_messages"_q,
		u"读取一个对话的最近文字消息。chat 留空表示当前打开的对话。返回里的 #编号用于转发。"_q,
		QJsonObject{
			{ u"chat"_q, Prop(u"string"_q, u"对话名称或用户名，留空表示当前对话"_q) },
			{ u"limit"_q, Prop(u"integer"_q, u"条数，默认 20，最多 40"_q) },
		},
		QJsonArray()));
	tools.append(Tool(
		u"open_chat"_q,
		u"在左侧打开一个对话，不发送消息。"_q,
		QJsonObject{
			{ u"chat"_q, Prop(u"string"_q, u"对话名称或用户名"_q) },
		},
		QJsonArray{ u"chat"_q }));
	tools.append(Tool(
		u"draft_message"_q,
		u"把文字放进对话输入框，不发送。"_q,
		QJsonObject{
			{ u"chat"_q, Prop(u"string"_q, u"对话名称，留空表示当前对话"_q) },
			{ u"text"_q, Prop(u"string"_q, u"草稿正文"_q) },
		},
		QJsonArray{ u"text"_q }));
	tools.append(Tool(
		u"send_message"_q,
		u"发送一条文字消息。会先等用户在面板里确认。"_q,
		QJsonObject{
			{ u"chat"_q, Prop(u"string"_q, u"对话名称，留空表示当前对话"_q) },
			{ u"text"_q, Prop(u"string"_q, u"要发送的正文"_q) },
		},
		QJsonArray{ u"text"_q }));
	tools.append(Tool(
		u"forward_message"_q,
		u"把一条已读到的消息转发到另一个对话。message_id 用 read_messages 结果里的编号。会先等用户确认。"_q,
		QJsonObject{
			{ u"from_chat"_q, Prop(u"string"_q, u"来源对话，留空表示当前对话"_q) },
			{ u"message_id"_q, Prop(u"string"_q, u"消息编号，不要带 #"_q) },
			{ u"to_chat"_q, Prop(u"string"_q, u"目标对话名称"_q) },
		},
		QJsonArray{ u"message_id"_q, u"to_chat"_q }));
	return tools;
}

[[nodiscard]] QJsonObject ParseArgs(const QJsonValue &value) {
	if (value.isObject()) {
		return value.toObject();
	}
	const auto document = QJsonDocument::fromJson(value.toString().toUtf8());
	return document.isObject() ? document.object() : QJsonObject();
}

[[nodiscard]] QString ToolName(const QJsonObject &call) {
	const auto function = call.value(u"function"_q).toObject();
	const auto name = function.value(u"name"_q).toString();
	return name.isEmpty() ? call.value(u"name"_q).toString() : name;
}

[[nodiscard]] QJsonObject ToolArgs(const QJsonObject &call) {
	const auto function = call.value(u"function"_q).toObject();
	if (!function.isEmpty()) {
		return ParseArgs(function.value(u"arguments"_q));
	}
	return ParseArgs(call.value(u"arguments"_q));
}

[[nodiscard]] QNetworkAccessManager &Network() {
	static QNetworkAccessManager manager;
	return manager;
}

} // namespace

Agent::Agent(
	not_null<Window::SessionController*> controller,
	not_null<Storage*> storage,
	Fn<void(bool stick)> refresh)
: _controller(controller)
, _storage(storage)
, _refresh(std::move(refresh)) {
}

Agent::~Agent() {
	++_generation;
	if (_requestId) {
		_controller->session().api().request(base::take(_requestId)).cancel();
	}
	if (const auto reply = _reply.data()) {
		QObject::disconnect(reply, nullptr, this, nullptr);
		reply->abort();
		reply->deleteLater();
		_reply = nullptr;
	}
}

bool Agent::running() const {
	return _running;
}

const std::vector<Bubble> &Agent::bubbles() const {
	return _bubbles;
}

QString Agent::activeId() const {
	const auto employee = _storage->active();
	return employee ? employee->id : QString();
}

void Agent::refresh(bool stick) {
	if (_refresh) {
		_refresh(stick);
	}
}

void Agent::persist() {
	const auto id = activeId();
	if (id.isEmpty()) {
		return;
	}
	_storage->setHistory(id, _api);
	_storage->save();
}

QString Agent::contextNote() const {
	QStringList lines;
	if (_storage->allowWrite()) {
		lines.push_back(
			u"「允许操作 TG」已打开。发送和转发仍会先停下来，等用户点确认。"_q);
	} else {
		lines.push_back(
			u"「允许操作 TG」是关闭的。send_message 和 forward_message 会失败。可以读取、打开对话和写草稿。"_q);
	}
	if (_storage->readCurrentChat()) {
		if (const auto history = _controller->activeChatCurrent().history()) {
			lines.push_back(u"当前打开的对话是「%1」。用户说当前聊天时就是它。read_messages、draft_message、send_message 的 chat 可以留空。"_q
				.arg(history->peer->name()));
		} else {
			lines.push_back(u"现在没有打开的对话。"_q);
		}
	} else {
		lines.push_back(u"用户关闭了「读取当前聊天」。不要假设当前打开的是哪个对话，调用工具时请写上对话名称。"_q);
	}
	return lines.join('\n');
}

QString Agent::systemPrompt() const {
	const auto employee = _storage->active();
	auto text = employee ? employee->prompt : QString();
	text += u"\n\n工具规则：\n"_q;
	text += u"1. list_folders 列出分组。list_chats 按名字或分组找对话。read_messages 读取最近消息。\n"_q;
	text += u"2. open_chat 只跳转。draft_message 只把文字放进输入框，不发送。\n"_q;
	text += u"3. send_message 和 forward_message 会等用户在面板里点「确认发布」后才执行。\n"_q;
	text += u"4. 总结和翻译直接写在回复里，不要为此发送消息。\n"_q;
	text += u"5. 没读到的内容不要编造。不要说已经发送，除非工具结果写了已提交。回复用简体中文。\n\n"_q;
	text += contextNote();
	return text;
}

void Agent::loadActive() {
	_api = _storage->history(activeId());
	_bubbles.clear();
	for (auto i = 0; i < _api.size(); ++i) {
		const auto object = _api.at(i).toObject();
		const auto role = object.value(u"role"_q).toString();
		const auto content = object.value(u"content"_q).toString().trimmed();
		if ((role == u"user"_q || role == u"assistant"_q) && !content.isEmpty()) {
			_bubbles.push_back(Bubble{ role, content });
		}
	}
	refresh(true);
}

void Agent::send(const QString &text) {
	const auto trimmed = text.trimmed();
	if (trimmed.isEmpty() || _running) {
		return;
	}
	if (!_storage->active()) {
		_bubbles.push_back(Bubble{ u"assistant"_q, u"请先添加一个员工。"_q });
		refresh(true);
		return;
	}
	if (_storage->apiKey().trimmed().isEmpty()) {
		_bubbles.push_back(Bubble{
			u"assistant"_q,
			u"请先点右上角齿轮，填写 xAI API 密钥。密钥只保存在本机。"_q,
		});
		refresh(true);
		return;
	}
	while (_api.size() > 36) {
		_api.removeAt(0);
	}
	RepairHistory(_api);
	QJsonObject user;
	user.insert(u"role"_q, u"user"_q);
	user.insert(u"content"_q, trimmed);
	_api.append(user);
	_bubbles.push_back(Bubble{ u"user"_q, trimmed });
	persist();
	_rounds = 0;
	_running = true;
	refresh(true);
	callModel();
}

void Agent::newChat() {
	if (_running) {
		pause();
	}
	_api = QJsonArray();
	_bubbles.clear();
	persist();
	refresh(true);
}

void Agent::pause() {
	if (!_running && !_waiting) {
		return;
	}
	++_generation;
	if (_requestId) {
		_controller->session().api().request(base::take(_requestId)).cancel();
	}
	if (const auto reply = _reply.data()) {
		QObject::disconnect(reply, nullptr, this, nullptr);
		reply->abort();
		reply->deleteLater();
		_reply = nullptr;
	}
	_running = false;
	_waiting = false;
	_toolCalls = QJsonArray();
	_toolIndex = 0;
	if (!_bubbles.empty() && _bubbles.back().role == u"card"_q) {
		_bubbles.back().showConfirm = false;
		_bubbles.back().showPause = false;
		for (auto &step : _bubbles.back().steps) {
			if (step.state == StepState::Running
				|| step.state == StepState::Waiting) {
				step.state = StepState::Failed;
				step.text = u"已暂停"_q;
			}
		}
	}
	RepairHistory(_api);
	persist();
	SetOperatingHistory(&_controller->session(), nullptr);
	refresh(true);
}

void Agent::confirm() {
	if (!_waiting) {
		return;
	}
	_waiting = false;
	if (!_bubbles.empty() && _bubbles.back().role == u"card"_q) {
		_bubbles.back().showConfirm = false;
		if (_pendingStep >= 0
			&& _pendingStep < int(_bubbles.back().steps.size())) {
			_bubbles.back().steps[_pendingStep].state = StepState::Running;
		}
	}
	refresh(true);
	const auto generation = _generation;
	const auto id = _pendingId;
	const auto stepIndex = _pendingStep;
	RunTool(
		_controller,
		this,
		_storage->readCurrentChat(),
		_pendingName,
		_pendingArgs,
		[=](ToolResult result) {
			if (generation != _generation) {
				return;
			}
			finishTool(id, stepIndex, result);
		});
}

void Agent::failRun(const QString &text) {
	++_generation;
	_running = false;
	_waiting = false;
	_toolCalls = QJsonArray();
	_toolIndex = 0;
	if (_reply) {
		QObject::disconnect(_reply, nullptr, this, nullptr);
		_reply->deleteLater();
		_reply = nullptr;
	}
	if (!_bubbles.empty() && _bubbles.back().role == u"card"_q) {
		_bubbles.back().showConfirm = false;
		_bubbles.back().showPause = false;
		for (auto &step : _bubbles.back().steps) {
			if (step.state == StepState::Running
				|| step.state == StepState::Waiting) {
				step.state = StepState::Failed;
			}
		}
	}
	_bubbles.push_back(Bubble{ u"assistant"_q, text });
	RepairHistory(_api);
	persist();
	SetOperatingHistory(&_controller->session(), nullptr);
	refresh(true);
}

void Agent::finishRun() {
	_running = false;
	_waiting = false;
	_toolCalls = QJsonArray();
	_toolIndex = 0;
	if (!_bubbles.empty() && _bubbles.back().role == u"card"_q) {
		_bubbles.back().showPause = false;
		_bubbles.back().showConfirm = false;
	}
	persist();
	SetOperatingHistory(&_controller->session(), nullptr);
	refresh(true);
}

void Agent::finishTool(
		const QString &id,
		int stepIndex,
		const ToolResult &result) {
	if (!_bubbles.empty() && _bubbles.back().role == u"card"_q) {
		auto &card = _bubbles.back();
		if (stepIndex >= 0 && stepIndex < int(card.steps.size())) {
			auto &step = card.steps[stepIndex];
			step.state = result.ok ? StepState::Done : StepState::Failed;
			if (!result.step.isEmpty()) {
				step.text = result.step;
			}
		}
		card.showConfirm = false;
	}
	QJsonObject tool;
	tool.insert(u"role"_q, u"tool"_q);
	tool.insert(u"tool_call_id"_q, id);
	tool.insert(u"content"_q, result.content);
	_api.append(tool);
	++_toolIndex;
	_waiting = false;
	refresh(true);
	const auto generation = _generation;
	crl::on_main(this, [=] {
		if (generation != _generation || !_running) {
			return;
		}
		processTools();
	});
}

void Agent::processTools() {
	if (_toolIndex >= _toolCalls.size()) {
		_toolCalls = QJsonArray();
		_toolIndex = 0;
		callModel();
		return;
	}
	const auto call = _toolCalls.at(_toolIndex).toObject();
	const auto id = [&] {
		const auto value = call.value(u"id"_q).toString();
		return value.isEmpty()
			? u"call-%1"_q.arg(_toolIndex)
			: value;
	}();
	const auto name = ToolName(call);
	const auto args = ToolArgs(call);
	if (_bubbles.empty() || _bubbles.back().role != u"card"_q) {
		_bubbles.push_back(Bubble{ u"card"_q });
	}
	auto &card = _bubbles.back();
	card.showPause = true;
	card.showConfirm = false;
	const auto stepIndex = int(card.steps.size());
	const auto blocked = IsWriteTool(name) && !_storage->allowWrite();
	card.steps.push_back(Step{
		DescribeTool(name, args),
		(IsWriteTool(name) && !blocked)
			? StepState::Waiting
			: StepState::Running,
	});
	if (blocked) {
		refresh(true);
		finishTool(id, stepIndex, {
			u"写操作已关闭：请先打开面板底部的「允许操作 TG」。"_q,
			u"写操作已关闭"_q,
			false,
		});
		return;
	}
	if (IsWriteTool(name)) {
		_waiting = true;
		_pendingId = id;
		_pendingName = name;
		_pendingArgs = args;
		_pendingStep = stepIndex;
		card.showConfirm = true;
		refresh(true);
		return;
	}
	refresh(true);
	const auto generation = _generation;
	RunTool(
		_controller,
		this,
		_storage->readCurrentChat(),
		name,
		args,
		[=](ToolResult result) {
			if (generation != _generation) {
				return;
			}
			finishTool(id, stepIndex, result);
		});
}

void Agent::callModel() {
	if (++_rounds > kMaxRounds) {
		failRun(u"本轮步骤已经比较多，先停一下。再发一句指令可以继续。"_q);
		return;
	}
	auto model = _storage->model().trimmed();
	if (model.isEmpty()) {
		model = u"grok-4"_q;
	}
	QJsonArray messages;
	QJsonObject system;
	system.insert(u"role"_q, u"system"_q);
	system.insert(u"content"_q, systemPrompt());
	messages.append(system);
	for (auto i = 0; i < _api.size(); ++i) {
		messages.append(_api.at(i));
	}
	QJsonObject body;
	body.insert(u"model"_q, model);
	body.insert(u"messages"_q, messages);
	body.insert(u"tools"_q, ToolSpecs());
	body.insert(u"tool_choice"_q, u"auto"_q);
	QNetworkRequest request(QUrl(QLatin1String(kEndpoint)));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"Authorization",
		"Bearer " + _storage->apiKey().trimmed().toUtf8());
	request.setRawHeader("User-Agent", "64Gram-Grok");
	request.setTransferTimeout(90000);
	const auto generation = _generation;
	const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
	const auto reply = Network().post(request, payload);
	_reply = reply;
	QObject::connect(reply, &QNetworkReply::finished, this, [=] {
		if (generation != _generation) {
			reply->deleteLater();
			return;
		}
		_reply = nullptr;
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto payload = reply->readAll();
		const auto networkError = (reply->error() != QNetworkReply::NoError);
		const auto errorString = reply->errorString();
		reply->deleteLater();
		if (networkError) {
			auto text = u"接口请求失败"_q;
			if (status == 401) {
				text = u"API 密钥无效，请在设置里检查。"_q;
			} else if (status == 429) {
				text = u"请求太频繁，稍后再试。"_q;
			} else if (!payload.isEmpty()) {
				text += u"：%1"_q.arg(QString::fromUtf8(payload).left(240));
			} else {
				text += u"：%1"_q.arg(errorString);
			}
			failRun(text);
			return;
		}
		const auto document = QJsonDocument::fromJson(payload);
		if (!document.isObject()) {
			failRun(u"接口返回了无法解析的内容。"_q);
			return;
		}
		const auto root = document.object();
		if (root.contains(u"error"_q)) {
			const auto message = root.value(u"error"_q).toObject().value(
				u"message"_q).toString();
			failRun(message.isEmpty() ? u"接口返回了错误。"_q : message);
			return;
		}
		const auto choices = root.value(u"choices"_q).toArray();
		if (choices.isEmpty()) {
			failRun(u"接口没有返回内容。"_q);
			return;
		}
		auto message = choices.at(0).toObject().value(u"message"_q).toObject();
		const auto content = message.value(u"content"_q).toString().trimmed();
		const auto toolCalls = message.value(u"tool_calls"_q).toArray();
		if (content.isEmpty() && toolCalls.isEmpty()) {
			failRun(u"接口没有返回内容。"_q);
			return;
		}
		if (message.value(u"role"_q).toString().isEmpty()) {
			message.insert(u"role"_q, u"assistant"_q);
		}
		if (!content.isEmpty()) {
			_bubbles.push_back(Bubble{ u"assistant"_q, content });
		}
		if (!toolCalls.isEmpty()) {
			if (content.isEmpty()) {
				message.insert(u"content"_q, QJsonValue::Null);
			}
			_api.append(message);
			for (auto &bubble : _bubbles) {
				bubble.showPause = false;
				bubble.showConfirm = false;
			}
			Bubble card;
			card.role = u"card"_q;
			card.showPause = true;
			_bubbles.push_back(card);
			_toolCalls = toolCalls;
			_toolIndex = 0;
			refresh(true);
			processTools();
			return;
		}
		_api.append(message);
		finishRun();
	});
}

} // namespace Grok
