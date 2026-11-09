/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <vector>

namespace Grok {

struct Employee {
	QString id;
	QString name;
	QString prompt;
	QString color;
	bool builtin = false;
};

void RepairHistory(QJsonArray &messages);
[[nodiscard]] QString EmployeeColor(int index);

class Storage {
public:
	void load();
	void save();

	[[nodiscard]] QString apiKey() const;
	void setApiKey(const QString &key);
	[[nodiscard]] QString model() const;
	void setModel(const QString &model);
	[[nodiscard]] bool allowWrite() const;
	void setAllowWrite(bool value);
	[[nodiscard]] bool readCurrentChat() const;
	void setReadCurrentChat(bool value);
	[[nodiscard]] bool panelOpen() const;
	void setPanelOpen(bool value);
	[[nodiscard]] int panelWidth() const;
	void setPanelWidth(int width);
	[[nodiscard]] bool didExtend() const;
	void setDidExtend(bool value);

	[[nodiscard]] const std::vector<Employee> &employees() const;
	[[nodiscard]] const Employee *active() const;
	[[nodiscard]] const Employee *find(const QString &id) const;
	void setActive(const QString &id);
	void addOrUpdate(Employee employee);
	void remove(const QString &id);
	void restorePresets();

	[[nodiscard]] QJsonArray history(const QString &employeeId) const;
	void setHistory(const QString &employeeId, QJsonArray messages);

private:
	void useDefaults();
	void ensureActive();
	[[nodiscard]] QString path() const;

	QString _apiKey;
	QString _model;
	bool _allowWrite = false;
	bool _readCurrentChat = true;
	bool _panelOpen = true;
	bool _didExtend = false;
	int _panelWidth = 0;
	QString _activeId;
	std::vector<Employee> _employees;
	QJsonObject _histories;

};

} // namespace Grok
