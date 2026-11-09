/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QString>

class QObject;

namespace Window {
class SessionController;
} // namespace Window

namespace Grok {

struct ToolResult {
	QString content;
	QString step;
	bool ok = true;
};

[[nodiscard]] bool IsWriteTool(const QString &name);
[[nodiscard]] QString DescribeTool(
	const QString &name,
	const QJsonObject &args);

void RunTool(
	not_null<Window::SessionController*> controller,
	not_null<QObject*> guard,
	bool allowCurrent,
	const QString &name,
	const QJsonObject &args,
	Fn<void(ToolResult)> done);

} // namespace Grok
