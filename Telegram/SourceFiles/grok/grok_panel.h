/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Grok {

class Panel;

class Host final {
public:
	explicit Host(not_null<Window::SessionController*> controller);
	~Host();

	void toggle();

private:
	const not_null<Window::SessionController*> _controller;
	Panel *_panel = nullptr;

};

void Toggle(not_null<Window::SessionController*> controller);

} // namespace Grok
