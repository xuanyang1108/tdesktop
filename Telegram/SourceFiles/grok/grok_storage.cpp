/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "grok/grok_storage.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>

namespace Grok {
namespace {

constexpr auto kMaxMessages = 40;

[[nodiscard]] QString RoleOf(const QJsonObject &message) {
	return message.value(u"role"_q).toString();
}

[[nodiscard]] QJsonArray ToolCallsOf(const QJsonObject &message) {
	return message.value(u"tool_calls"_q).toArray();
}

[[nodiscard]] Employee MakePreset(
		const QString &id,
		const QString &name,
		const QString &color,
		const QString &prompt) {
	return Employee{
		.id = id,
		.name = name,
		.prompt = prompt,
		.color = color,
		.builtin = true,
	};
}

[[nodiscard]] std::vector<Employee> Presets() {
	return {
		MakePreset(
			u"reviewer"_q,
			u"审核员"_q,
			u"#E8A317"_q,
			u"你是「审核员」，负责检查用户指定的投稿群、频道或对话里的内容是否合格。先用工具找到目标并读取最近消息，再按用户给出的标准逐条判断，写出理由。用户没明确要求发布时，只给结论，不要发送。需要把合格内容发到某个频道时，对每一条调用 send_message 或 forward_message。"_q),
		MakePreset(
			u"support"_q,
			u"客服"_q,
			u"#3DDC97"_q,
			u"你是「客服」。先阅读用户指出的客户对话，理解对方最后一个问题，起草一版礼貌、简短、可以直接发送的回复。默认只用 draft_message 把回复放进输入框。只有用户明确说发送或回复出去时才调用 send_message。不要一次给很多备选。"_q),
		MakePreset(
			u"poster"_q,
			u"发帖员"_q,
			u"#3D8BFD"_q,
			u"你是「发帖员」。按用户的要求起草频道或群消息。先确认目标对话存在。默认用 draft_message 放进输入框。用户明确说发布或发送时才 send_message。转发已有消息用 forward_message。真正发布前用一两句话复述将要发出的内容和目标。"_q),
		MakePreset(
			u"analyst"_q,
			u"数据员"_q,
			u"#A78BFA"_q,
			u"你是「数据员」，负责汇总、翻译和提取对话里的信息。用 list_chats、list_folders、read_messages 收集内容，再用文字给出摘要、翻译、名单或简单统计。不要发送或转发，除非用户明确要求。数字和原话必须来自读到的消息，读不到就说明没读到。"_q),
	};
}

} // namespace

void RepairHistory(QJsonArray &messages) {
	while (!messages.isEmpty()
		&& RoleOf(messages.at(0).toObject()) == u"tool"_q) {
		messages.removeAt(0);
	}
	auto index = 0;
	while (index < messages.size()) {
		const auto message = messages.at(index).toObject();
		const auto calls = ToolCallsOf(message);
		if (RoleOf(message) != u"assistant"_q || calls.isEmpty()) {
			++index;
			continue;
		}
		base::flat_set<QString> pending;
		for (auto i = 0; i < calls.size(); ++i) {
			pending.insert(calls.at(i).toObject().value(u"id"_q).toString());
		}
		auto cursor = index + 1;
		while (cursor < messages.size()
			&& RoleOf(messages.at(cursor).toObject()) == u"tool"_q) {
			pending.remove(messages.at(cursor).toObject().value(
				u"tool_call_id"_q).toString());
			++cursor;
		}
		if (!pending.empty()) {
			for (auto drop = cursor - 1; drop >= index; --drop) {
				messages.removeAt(drop);
			}
			continue;
		}
		index = cursor;
	}
}

QString EmployeeColor(int index) {
	static const QStringList kColors = {
		u"#E8A317"_q,
		u"#3DDC97"_q,
		u"#3D8BFD"_q,
		u"#A78BFA"_q,
		u"#F07178"_q,
		u"#7FD1FF"_q,
	};
	const auto wrapped = index % kColors.size();
	return kColors[wrapped < 0 ? 0 : wrapped];
}

QString Storage::path() const {
	return cWorkingDir() + u"tdata/grok_employees.json"_q;
}

void Storage::useDefaults() {
	_apiKey = QString();
	_model = u"grok-4"_q;
	_allowWrite = false;
	_readCurrentChat = true;
	_panelOpen = true;
	_didExtend = false;
	_panelWidth = 0;
	_employees = Presets();
	_activeId = _employees.front().id;
	_histories = QJsonObject();
}

void Storage::ensureActive() {
	if (_employees.empty()) {
		_activeId = QString();
		return;
	}
	if (!find(_activeId)) {
		_activeId = _employees.front().id;
	}
}

void Storage::load() {
	useDefaults();
	QFile file(path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(file.readAll());
	if (!doc.isObject()) {
		return;
	}
	const auto root = doc.object();
	_apiKey = root.value(u"apiKey"_q).toString();
	_model = root.value(u"model"_q).toString().trimmed();
	if (_model.isEmpty()) {
		_model = u"grok-4"_q;
	}
	_allowWrite = root.value(u"allowWrite"_q).toBool(false);
	_readCurrentChat = root.value(u"readCurrentChat"_q).toBool(true);
	_panelOpen = root.value(u"panelOpen"_q).toBool(true);
	_didExtend = root.value(u"didExtend"_q).toBool(false);
	_panelWidth = root.value(u"panelWidth"_q).toInt(0);
	_activeId = root.value(u"activeEmployee"_q).toString();
	_employees.clear();
	const auto employees = root.value(u"employees"_q).toArray();
	for (auto i = 0; i < employees.size(); ++i) {
		const auto object = employees.at(i).toObject();
		auto employee = Employee{
			.id = object.value(u"id"_q).toString(),
			.name = object.value(u"name"_q).toString().trimmed(),
			.prompt = object.value(u"prompt"_q).toString(),
			.color = object.value(u"color"_q).toString(),
			.builtin = object.value(u"builtin"_q).toBool(false),
		};
		if (employee.id.isEmpty() || employee.name.isEmpty()) {
			continue;
		}
		if (!employee.color.startsWith('#')) {
			employee.color = EmployeeColor(int(_employees.size()));
		}
		_employees.push_back(std::move(employee));
	}
	if (_employees.empty()) {
		_employees = Presets();
		_activeId = _employees.front().id;
	}
	ensureActive();
	_histories = root.value(u"histories"_q).toObject();
	const auto keys = _histories.keys();
	for (const auto &key : keys) {
		auto messages = _histories.value(key).toArray();
		RepairHistory(messages);
		_histories.insert(key, messages);
	}
}

void Storage::save() {
	ensureActive();
	QJsonArray employees;
	for (const auto &employee : _employees) {
		QJsonObject object;
		object.insert(u"id"_q, employee.id);
		object.insert(u"name"_q, employee.name);
		object.insert(u"prompt"_q, employee.prompt);
		object.insert(u"color"_q, employee.color);
		object.insert(u"builtin"_q, employee.builtin);
		employees.append(object);
	}
	QJsonObject root;
	root.insert(u"version"_q, 1);
	root.insert(u"apiKey"_q, _apiKey);
	root.insert(u"model"_q, _model);
	root.insert(u"allowWrite"_q, _allowWrite);
	root.insert(u"readCurrentChat"_q, _readCurrentChat);
	root.insert(u"panelOpen"_q, _panelOpen);
	root.insert(u"didExtend"_q, _didExtend);
	root.insert(u"panelWidth"_q, _panelWidth);
	root.insert(u"activeEmployee"_q, _activeId);
	root.insert(u"employees"_q, employees);
	root.insert(u"histories"_q, _histories);

	const auto destination = path();
	QDir().mkpath(QFileInfo(destination).absolutePath());
	const auto temporary = destination + u".tmp"_q;
	QFile file(temporary);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	file.close();
	QFile::remove(destination);
	QFile::rename(temporary, destination);
}

QString Storage::apiKey() const {
	return _apiKey;
}

void Storage::setApiKey(const QString &key) {
	_apiKey = key.trimmed();
}

QString Storage::model() const {
	return _model;
}

void Storage::setModel(const QString &model) {
	_model = model.trimmed();
}

bool Storage::allowWrite() const {
	return _allowWrite;
}

void Storage::setAllowWrite(bool value) {
	_allowWrite = value;
}

bool Storage::readCurrentChat() const {
	return _readCurrentChat;
}

void Storage::setReadCurrentChat(bool value) {
	_readCurrentChat = value;
}

bool Storage::panelOpen() const {
	return _panelOpen;
}

void Storage::setPanelOpen(bool value) {
	_panelOpen = value;
}

int Storage::panelWidth() const {
	return _panelWidth;
}

void Storage::setPanelWidth(int width) {
	_panelWidth = width;
}

bool Storage::didExtend() const {
	return _didExtend;
}

void Storage::setDidExtend(bool value) {
	_didExtend = value;
}

const std::vector<Employee> &Storage::employees() const {
	return _employees;
}

const Employee *Storage::active() const {
	return find(_activeId);
}

const Employee *Storage::find(const QString &id) const {
	for (const auto &employee : _employees) {
		if (employee.id == id) {
			return &employee;
		}
	}
	return nullptr;
}

void Storage::setActive(const QString &id) {
	if (find(id)) {
		_activeId = id;
	}
}

void Storage::addOrUpdate(Employee employee) {
	if (employee.id.isEmpty() || employee.name.trimmed().isEmpty()) {
		return;
	}
	employee.name = employee.name.trimmed();
	for (auto &current : _employees) {
		if (current.id == employee.id) {
			current = std::move(employee);
			ensureActive();
			return;
		}
	}
	if (_activeId.isEmpty()) {
		_activeId = employee.id;
	}
	_employees.push_back(std::move(employee));
}

void Storage::remove(const QString &id) {
	_employees.erase(
		std::remove_if(
			_employees.begin(),
			_employees.end(),
			[&](const Employee &employee) { return employee.id == id; }),
		_employees.end());
	_histories.remove(id);
	ensureActive();
}

void Storage::restorePresets() {
	for (const auto &preset : Presets()) {
		auto replaced = false;
		for (auto &employee : _employees) {
			if (employee.id == preset.id) {
				employee.name = preset.name;
				employee.prompt = preset.prompt;
				employee.color = preset.color;
				employee.builtin = true;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			_employees.push_back(preset);
		}
	}
	ensureActive();
}

QJsonArray Storage::history(const QString &employeeId) const {
	auto messages = _histories.value(employeeId).toArray();
	RepairHistory(messages);
	return messages;
}

void Storage::setHistory(const QString &employeeId, QJsonArray messages) {
	while (messages.size() > kMaxMessages) {
		messages.removeAt(0);
	}
	RepairHistory(messages);
	if (employeeId.isEmpty() || messages.isEmpty()) {
		_histories.remove(employeeId);
		return;
	}
	_histories.insert(employeeId, messages);
}

} // namespace Grok
