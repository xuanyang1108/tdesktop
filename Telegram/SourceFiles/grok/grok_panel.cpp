/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "grok/grok_panel.h"

#include "grok/grok_api.h"
#include "mainwindow.h"
#include "styles/style_grok.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "window/window_session_controller.h"

#include <QtWidgets/QScrollBar>

#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>

namespace Grok {
namespace {

const auto kBg = QColor(0x16, 0x18, 0x1D);
const auto kCard = QColor(0x22, 0x26, 0x2E);
const auto kUser = QColor(0x1D, 0x4E, 0x89);
const auto kText = QColor(0xE7, 0xE9, 0xEA);
const auto kMuted = QColor(0x8B, 0x98, 0xA5);
const auto kAccent = QColor(0x3D, 0x8B, 0xFD);
const auto kOk = QColor(0x3D, 0xDC, 0x97);
const auto kBad = QColor(0xF0, 0x71, 0x78);
const auto kWait = QColor(0xE8, 0xA3, 0x17);
const auto kLine = QColor(0x3A, 0x3F, 0x48);

[[nodiscard]] int WrappedHeight(const QFont &font, const QString &text, int width) {
	if (text.isEmpty() || width <= 0) {
		return 0;
	}
	return QFontMetrics(font).boundingRect(
		QRect(0, 0, width, 100000),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
		text).height();
}

QHash<Window::SessionController*, Fn<void()>> &Toggles() {
	static QHash<Window::SessionController*, Fn<void()>> map;
	return map;
}

void RegisterToggle(
		Window::SessionController *controller,
		Fn<void()> toggle) {
	if (!controller) {
		return;
	} else if (toggle) {
		Toggles().insert(controller, std::move(toggle));
	} else {
		Toggles().remove(controller);
	}
}

class Transcript final : public QWidget {
public:
	explicit Transcript(QWidget *parent) : QWidget(parent) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	void setBubbles(const std::vector<Bubble> &bubbles) {
		_bubbles = bubbles;
		const auto used = std::max(width(), st::grokPanelMinWidth);
		setMinimumHeight(std::max(1, render(nullptr, used)));
		update();
	}
	void setConfirm(Fn<void()> callback) {
		_onConfirm = std::move(callback);
	}
	void setPause(Fn<void()> callback) {
		_onPause = std::move(callback);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		auto hq = PainterHighQualityEnabler(p);
		p.setClipRect(e->rect());
		render(&p, width());
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (_hasConfirm && _confirm.contains(e->pos()) && _onConfirm) {
			_onConfirm();
		} else if (_hasPause && _pause.contains(e->pos()) && _onPause) {
			_onPause();
		}
	}

private:
	int render(QPainter *p, int width);
	int paintBubble(QPainter *p, const Bubble &bubble, int x, int y, int inner);

	std::vector<Bubble> _bubbles;
	QRect _confirm;
	QRect _pause;
	bool _hasConfirm = false;
	bool _hasPause = false;
	Fn<void()> _onConfirm;
	Fn<void()> _onPause;

};

int Transcript::paintBubble(
		QPainter *p,
		const Bubble &bubble,
		int x,
		int y,
		int inner) {
	const auto font = st::normalFont->f;
	if (bubble.role == u"card"_q) {
		auto cardHeight = st::grokPad + st::semiboldFont->height + st::grokGap;
		for (const auto &step : bubble.steps) {
			cardHeight += st::normalFont->height + st::grokGap;
		}
		if (!bubble.steps.empty()) {
			cardHeight += st::grokGap + st::lineWidth * 4;
		}
		if (bubble.showPause || bubble.showConfirm) {
			cardHeight += st::grokGap + st::grokButtonHeight;
		}
		cardHeight += st::grokPad;
		if (!p) {
			return cardHeight;
		}
		p->setPen(Qt::NoPen);
		p->setBrush(kCard);
		p->drawRoundedRect(x, y, inner, cardHeight, st::grokRadius, st::grokRadius);
		auto cursor = y + st::grokPad;
		p->setPen(kText);
		p->setFont(st::semiboldFont);
		p->drawText(
			QRect(x + st::grokPad, cursor, inner - st::grokPad * 2, st::semiboldFont->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			u"任务进度"_q);
		cursor += st::semiboldFont->height + st::grokGap;
		p->setFont(font);
		auto done = 0;
		for (const auto &step : bubble.steps) {
			if (step.state == StepState::Done || step.state == StepState::Failed) {
				++done;
			}
			const auto mark = (step.state == StepState::Done)
				? u"✓"_q
				: (step.state == StepState::Failed)
				? u"✕"_q
				: (step.state == StepState::Waiting)
				? u"…"_q
				: u"•"_q;
			p->setPen((step.state == StepState::Done)
				? kOk
				: (step.state == StepState::Failed)
				? kBad
				: (step.state == StepState::Waiting)
				? kWait
				: kAccent);
			p->drawText(
				QRect(x + st::grokPad, cursor, st::grokPad * 2, st::normalFont->height),
				Qt::AlignCenter,
				mark);
			p->setPen(kText);
			p->drawText(
				QRect(
					x + st::grokPad * 3,
					cursor,
					inner - st::grokPad * 4,
					st::normalFont->height),
				Qt::AlignLeft | Qt::AlignVCenter,
				st::normalFont->elided(step.text, inner - st::grokPad * 4));
			cursor += st::normalFont->height + st::grokGap;
		}
		if (!bubble.steps.empty()) {
			const auto bar = QRect(
				x + st::grokPad,
				cursor,
				inner - st::grokPad * 2,
				st::lineWidth * 4);
			p->setPen(Qt::NoPen);
			p->setBrush(kLine);
			p->drawRoundedRect(bar, bar.height() / 2., bar.height() / 2.);
			const auto ratio = double(done) / double(bubble.steps.size());
			p->setBrush(kAccent);
			p->drawRoundedRect(
				QRect(bar.x(), bar.y(), int(bar.width() * ratio), bar.height()),
				bar.height() / 2.,
				bar.height() / 2.);
			cursor += bar.height() + st::grokGap;
		}
		if (bubble.showPause || bubble.showConfirm) {
			const auto gap = st::grokGap;
			const auto both = bubble.showPause && bubble.showConfirm;
			const auto buttonWidth = both
				? (inner - st::grokPad * 2 - gap) / 2
				: (inner - st::grokPad * 2);
			if (bubble.showPause) {
				_pause = QRect(x + st::grokPad, cursor, buttonWidth, st::grokButtonHeight);
				_hasPause = true;
				p->setPen(QPen(kLine, st::lineWidth));
				p->setBrush(Qt::NoBrush);
				p->drawRoundedRect(_pause, st::grokRadius, st::grokRadius);
				p->setPen(kText);
				p->drawText(_pause, Qt::AlignCenter, u"暂停"_q);
			}
			if (bubble.showConfirm) {
				const auto left = both
					? x + st::grokPad + buttonWidth + gap
					: x + st::grokPad;
				_confirm = QRect(left, cursor, buttonWidth, st::grokButtonHeight);
				_hasConfirm = true;
				p->setPen(Qt::NoPen);
				p->setBrush(kAccent);
				p->drawRoundedRect(_confirm, st::grokRadius, st::grokRadius);
				p->setPen(Qt::white);
				p->drawText(_confirm, Qt::AlignCenter, u"确认发布"_q);
			}
		}
		return cardHeight;
	}
	const auto mine = (bubble.role == u"user"_q);
	const auto textWidth = std::max(
		st::grokAvatar * 2,
		inner - (mine ? 0 : st::grokAvatar / 2 + st::grokGap));
	const auto usedWidth = std::min(textWidth, int(inner * 0.86));
	const auto textH = std::max(
		st::normalFont->height,
		WrappedHeight(font, bubble.text, usedWidth - st::grokPad * 2));
	const auto bubbleH = textH + st::grokPad * 2;
	const auto bubbleX = mine
		? x + inner - usedWidth
		: x + st::grokAvatar / 2 + st::grokGap;
	if (p) {
		if (!mine) {
			const auto avatar = QRect(
				x,
				y,
				st::grokAvatar / 2,
				st::grokAvatar / 2);
			p->setPen(Qt::NoPen);
			p->setBrush(kAccent);
			p->drawEllipse(avatar);
			p->setPen(Qt::white);
			p->setFont(st::semiboldFont);
			p->drawText(avatar, Qt::AlignCenter, u"G"_q);
		}
		p->setPen(Qt::NoPen);
		p->setBrush(mine ? kUser : kCard);
		p->drawRoundedRect(
			bubbleX,
			y,
			usedWidth,
			bubbleH,
			st::grokRadius,
			st::grokRadius);
		p->setPen(kText);
		p->setFont(font);
		p->drawText(
			QRect(
				bubbleX + st::grokPad,
				y + st::grokPad,
				usedWidth - st::grokPad * 2,
				textH),
			Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
			bubble.text);
	}
	return std::max(bubbleH, mine ? 0 : st::grokAvatar / 2);
}

int Transcript::render(QPainter *p, int width) {
	_confirm = {};
	_pause = {};
	_hasConfirm = false;
	_hasPause = false;
	if (p) {
		p->fillRect(0, 0, width, std::max(height(), 1), kBg);
	}
	if (_bubbles.empty()) {
		const auto bounds = QRect(
			st::grokPad,
			st::grokPad,
			std::max(1, width - st::grokPad * 2),
			st::grokHeaderHeight * 2);
		if (p) {
			p->setPen(kMuted);
			p->setFont(st::normalFont);
			p->drawText(
				bounds,
				Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
				u"选一个员工，用中文下指令。例如：把「投稿-审核」里最近的消息总结一下。"_q);
		}
		return bounds.bottom() + st::grokPad;
	}
	auto y = st::grokPad;
	const auto inner = std::max(st::grokAvatar * 3, width - st::grokPad * 2);
	for (const auto &bubble : _bubbles) {
		y += paintBubble(p, bubble, st::grokPad, y, inner) + st::grokGap;
	}
	return y + st::grokPad;
}

} // namespace

class Panel final : public Ui::RpWidget {
public:
	Panel(QWidget *parent, not_null<Window::SessionController*> controller);
	~Panel();

	void toggleOpen();
	[[nodiscard]] int takeInitialExtend();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Hit {
		QRect rect;
		QString action;
	};
	struct AvatarSlot {
		QRect circle;
		QString id;
		QString name;
		QString color;
		bool selected = false;
		bool add = false;
	};

	void setupWidgets();
	void layoutChildren();
	void recountHits();
	void applyWidth();
	void applyChrome();
	void setOpen(bool open);
	void submit();
	void refreshFromAgent(bool stick);
	void activateEmployee(const QString &id);
	void openSettings(bool editor);
	void closeSettings();
	void rebuildSettings();
	void pullSettings();

	const not_null<Window::SessionController*> _controller;
	Storage _storage;
	std::unique_ptr<Agent> _agent;
	Transcript *_transcript = nullptr;
	QScrollArea *_scroll = nullptr;
	QScrollArea *_settingsScroll = nullptr;
	QLineEdit *_input = nullptr;
	QPointer<QLineEdit> _keyEdit;
	QPointer<QLineEdit> _modelEdit;
	QPointer<QCheckBox> _readBox;
	std::vector<Hit> _hits;
	std::vector<AvatarSlot> _avatars;
	QRect _sendRect;
	bool _open = true;
	bool _settingsMode = false;
	bool _editor = false;
	bool _resizing = false;
	int _dragX = 0;
	int _dragWidth = 0;
	QString _editorId;

};

Panel::Panel(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	_storage.load();
	_open = _storage.panelOpen();
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);
	auto palette = this->palette();
	palette.setColor(QPalette::Window, kBg);
	setPalette(palette);
	setAutoFillBackground(true);
	setupWidgets();
	_agent = std::make_unique<Agent>(
		_controller,
		&_storage,
		[=](bool stick) { refreshFromAgent(stick); });
	_agent->loadActive();
	applyWidth();
	applyChrome();
}

Panel::~Panel() {
	pullSettings();
}

void Panel::setupWidgets() {
	const auto paintScroll = [&](QScrollArea *area) {
		area->setFrameShape(QFrame::NoFrame);
		area->setWidgetResizable(true);
		area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		area->setPalette(palette());
		area->viewport()->setAutoFillBackground(true);
		area->viewport()->setPalette(palette());
	};
	_scroll = new QScrollArea(this);
	_transcript = new Transcript(_scroll);
	_scroll->setWidget(_transcript);
	paintScroll(_scroll);
	_transcript->setConfirm([=] { _agent->confirm(); });
	_transcript->setPause([=] { _agent->pause(); });

	_settingsScroll = new QScrollArea(this);
	paintScroll(_settingsScroll);
	_settingsScroll->hide();

	_input = new QLineEdit(this);
	_input->setFont(st::normalFont->f);
	_input->setStyleSheet(
		u"QLineEdit { background:#22262e; color:#e7e9ea; border:none;"
		u" border-radius:%1px; padding:0 12px; }"_q.arg(st::grokRadius));
	QObject::connect(_input, &QLineEdit::returnPressed, this, [=] {
		submit();
	});
}

void Panel::applyWidth() {
	auto width = _storage.panelWidth();
	if (width <= 0) {
		width = st::grokPanelDefaultWidth;
	}
	width = std::clamp(width, st::grokPanelMinWidth, st::grokPanelMaxWidth);
	setFixedWidth(_open ? width : st::grokRailWidth);
}

int Panel::takeInitialExtend() {
	if (_storage.didExtend() || !_open) {
		return 0;
	}
	_storage.setDidExtend(true);
	_storage.save();
	return std::max(0, width() - st::grokRailWidth);
}

void Panel::toggleOpen() {
	setOpen(!_open);
}

void Panel::setOpen(bool open) {
	if (_open == open) {
		return;
	}
	const auto before = width();
	_open = open;
	_storage.setPanelOpen(open);
	_storage.save();
	applyWidth();
	const auto delta = width() - before;
	if (delta > 0 && !_controller->widget()->isMaximized()) {
		_controller->widget()->tryToExtendWidthBy(delta);
	}
	_controller->widget()->refreshGrokPanel();
	layoutChildren();
	applyChrome();
	update();
}

void Panel::applyChrome() {
	const auto employee = _storage.active();
	if (_agent && _agent->running()) {
		_input->setPlaceholderText(u"处理中，可点暂停…"_q);
	} else if (employee) {
		_input->setPlaceholderText(u"给 %1 下指令…"_q.arg(employee->name));
	} else {
		_input->setPlaceholderText(u"先添加一个员工…"_q);
	}
}

void Panel::layoutChildren() {
	recountHits();
	if (!_open) {
		_scroll->hide();
		_settingsScroll->hide();
		_input->hide();
		return;
	}
	const auto handle = st::grokResizeWidth;
	const auto top = st::grokHeaderHeight + st::grokEmployeesHeight;
	const auto inputH = _settingsMode ? 0 : st::grokInputHeight;
	const auto fieldTop = height() - st::grokFooterHeight - inputH;
	const auto midH = std::max(0, fieldTop - top);
	_scroll->setGeometry(handle, top, std::max(0, width() - handle), midH);
	_settingsScroll->setGeometry(_scroll->geometry());
	_scroll->setVisible(!_settingsMode);
	_settingsScroll->setVisible(_settingsMode);
	if (_settingsMode) {
		_input->hide();
	} else {
		_input->setGeometry(
			st::grokPad,
			fieldTop + st::grokGap / 2,
			std::max(1, width() - st::grokPad * 2 - st::grokButtonHeight - st::grokGap),
			std::max(1, st::grokInputHeight - st::grokGap));
		_input->show();
	}
}

void Panel::recountHits() {
	_hits.clear();
	_avatars.clear();
	if (!_open) {
		_hits.push_back({ rect(), u"open"_q });
		return;
	}
	const auto button = st::grokButtonHeight;
	const auto y = (st::grokHeaderHeight - button) / 2;
	auto x = width() - st::grokPad;
	const auto collapse = QRect(x - button, y, button, button);
	x = collapse.x() - st::grokGap;
	const auto settings = QRect(x - button, y, button, button);
	x = settings.x() - st::grokGap;
	const auto neu = QRect(x - button * 2, y, button * 2, button);
	_hits.push_back({ collapse, u"collapse"_q });
	_hits.push_back({ settings, u"settings"_q });
	_hits.push_back({ neu, u"new"_q });

	const auto slot = st::grokAvatar + st::grokPad;
	const auto available = std::max(slot, width() - st::grokPad * 2);
	const auto slots = std::max(1, available / slot);
	const auto shown = std::min(
		int(_storage.employees().size()),
		std::max(0, slots - 1));
	auto ex = st::grokPad;
	const auto top = st::grokHeaderHeight + st::grokGap;
	for (auto i = 0; i < shown; ++i) {
		const auto &employee = _storage.employees()[i];
		const auto circle = QRect(ex, top, st::grokAvatar, st::grokAvatar);
		_avatars.push_back({
			circle,
			employee.id,
			employee.name,
			employee.color,
			_storage.active() && _storage.active()->id == employee.id,
			false,
		});
		_hits.push_back({
			circle.adjusted(0, 0, 0, st::normalFont->height + st::grokGap),
			u"employee:"_q + employee.id,
		});
		ex += slot;
	}
	_avatars.push_back({
		QRect(ex, top, st::grokAvatar, st::grokAvatar),
		QString(),
		u"添加员工"_q,
		QString(),
		false,
		true,
	});
	_hits.push_back({
		_avatars.back().circle.adjusted(
			0,
			0,
			0,
			st::normalFont->height + st::grokGap),
		u"add"_q,
	});

	const auto fieldTop = height() - st::grokFooterHeight - (
		_settingsMode ? 0 : st::grokInputHeight);
	_sendRect = QRect(
		width() - st::grokPad - st::grokButtonHeight,
		fieldTop + (st::grokInputHeight - st::grokButtonHeight) / 2,
		st::grokButtonHeight,
		st::grokButtonHeight);
	if (!_settingsMode) {
		_hits.push_back({ _sendRect, u"send"_q });
	}
	const auto toggle = QRect(
		st::grokPad,
		height() - st::grokFooterHeight
			+ (st::grokFooterHeight - st::grokToggleHeight) / 2,
		st::grokToggleWidth,
		st::grokToggleHeight);
	_hits.push_back({ toggle.adjusted(-st::grokGap, -st::grokGap, st::grokPad * 8, st::grokGap), u"toggle"_q });
}

void Panel::resizeEvent(QResizeEvent *e) {
	RpWidget::resizeEvent(e);
	layoutChildren();
}

void Panel::mousePressEvent(QMouseEvent *e) {
	if (!_open) {
		setOpen(true);
		return;
	}
	if (e->pos().x() <= st::grokResizeWidth) {
		_resizing = true;
		_dragX = e->globalPos().x();
		_dragWidth = width();
		return;
	}
	for (const auto &hit : _hits) {
		if (!hit.rect.contains(e->pos())) {
			continue;
		}
		if (hit.action == u"collapse"_q) {
			setOpen(false);
		} else if (hit.action == u"settings"_q) {
			openSettings(false);
		} else if (hit.action == u"new"_q) {
			_agent->newChat();
		} else if (hit.action == u"add"_q) {
			openSettings(true);
		} else if (hit.action == u"send"_q) {
			submit();
		} else if (hit.action == u"toggle"_q) {
			_storage.setAllowWrite(!_storage.allowWrite());
			_storage.save();
			update();
		} else if (hit.action.startsWith(u"employee:"_q)) {
			activateEmployee(hit.action.mid(u"employee:"_q.size()));
		}
		return;
	}
}

void Panel::mouseMoveEvent(QMouseEvent *e) {
	if (_resizing) {
		const auto next = std::clamp(
			_dragWidth + (_dragX - e->globalPos().x()),
			st::grokPanelMinWidth,
			st::grokPanelMaxWidth);
		if (next != width()) {
			setFixedWidth(next);
			_controller->widget()->refreshGrokPanel();
		}
		return;
	}
	if (_open && e->pos().x() <= st::grokResizeWidth) {
		setCursor(Qt::SplitHCursor);
	} else {
		unsetCursor();
	}
}

void Panel::mouseReleaseEvent(QMouseEvent *e) {
	if (!_resizing) {
		return;
	}
	_resizing = false;
	_storage.setPanelWidth(width());
	_storage.save();
	RpWidget::mouseReleaseEvent(e);
}

void Panel::submit() {
	if (!_agent || _agent->running()) {
		return;
	}
	const auto text = _input->text().trimmed();
	if (text.isEmpty()) {
		return;
	}
	_input->clear();
	_agent->send(text);
}

void Panel::refreshFromAgent(bool stick) {
	const auto bar = _scroll->verticalScrollBar();
	const auto nearBottom = (bar->maximum() - bar->value()) < st::grokButtonHeight;
	_transcript->setBubbles(_agent->bubbles());
	if (stick || nearBottom) {
		QTimer::singleShot(0, this, [=] {
			const auto current = _scroll->verticalScrollBar();
			current->setValue(current->maximum());
		});
	}
	applyChrome();
	update();
}

void Panel::activateEmployee(const QString &id) {
	if (_storage.active() && _storage.active()->id == id) {
		return;
	}
	if (_agent->running()) {
		_agent->pause();
	}
	_storage.setActive(id);
	_storage.save();
	_agent->loadActive();
	recountHits();
	applyChrome();
	update();
}

void Panel::openSettings(bool editor) {
	_settingsMode = true;
	_editor = editor;
	_editorId = QString();
	rebuildSettings();
	layoutChildren();
	update();
}

void Panel::closeSettings() {
	pullSettings();
	_settingsMode = false;
	_editor = false;
	layoutChildren();
	applyChrome();
	update();
}

void Panel::pullSettings() {
	auto changed = false;
	if (_keyEdit) {
		_storage.setApiKey(_keyEdit->text());
		changed = true;
	}
	if (_modelEdit) {
		_storage.setModel(_modelEdit->text());
		changed = true;
	}
	if (_readBox) {
		_storage.setReadCurrentChat(_readBox->isChecked());
		changed = true;
	}
	if (changed) {
		_storage.save();
	}
}

void Panel::rebuildSettings() {
	pullSettings();
	_keyEdit = nullptr;
	_modelEdit = nullptr;
	_readBox = nullptr;
	auto *inner = new QWidget();
	inner->setStyleSheet(
		u"QLabel { color:#e7e9ea; }"
		u"QCheckBox { color:#e7e9ea; }"
		u"QLineEdit, QPlainTextEdit { background:#22262e; color:#e7e9ea;"
		u" border:1px solid #3a3f48; border-radius:8px; padding:6px; }"
		u"QPushButton { background:#2a3140; color:#e7e9ea; border:none;"
		u" border-radius:8px; padding:6px 10px; }"_q);
	auto *layout = new QVBoxLayout(inner);
	layout->setContentsMargins(st::grokPad, st::grokPad, st::grokPad, st::grokPad);
	layout->setSpacing(st::grokGap);
	const auto addLabel = [&](const QString &text) {
		layout->addWidget(new QLabel(text, inner));
	};
	const auto addButton = [&](const QString &text) {
		return new QPushButton(text, inner);
	};
	if (_editor) {
		const auto current = _storage.find(_editorId);
		addLabel(current ? u"编辑员工"_q : u"添加员工"_q);
		addLabel(u"名称"_q);
		const auto nameEdit = new QLineEdit(inner);
		nameEdit->setText(current ? current->name : QString());
		layout->addWidget(nameEdit);
		addLabel(u"系统提示"_q);
		const auto promptEdit = new QPlainTextEdit(inner);
		promptEdit->setPlainText(current ? current->prompt : QString());
		promptEdit->setMinimumHeight(st::grokHeaderHeight * 3);
		layout->addWidget(promptEdit);
		const auto save = addButton(u"保存"_q);
		const auto cancel = addButton(u"取消"_q);
		layout->addWidget(save);
		layout->addWidget(cancel);
		layout->addStretch();
		QObject::connect(cancel, &QPushButton::clicked, this, [=] {
			_editor = false;
			rebuildSettings();
		});
		QObject::connect(save, &QPushButton::clicked, this, [=] {
			auto employee = current ? *current : Employee();
			employee.name = nameEdit->text().trimmed();
			employee.prompt = promptEdit->toPlainText().trimmed();
			if (employee.name.isEmpty()) {
				return;
			}
			if (employee.id.isEmpty()) {
				employee.id = u"e%1"_q.arg(QDateTime::currentMSecsSinceEpoch());
				employee.color = EmployeeColor(int(_storage.employees().size()));
				employee.builtin = false;
			}
			if (employee.prompt.isEmpty()) {
				employee.prompt = u"你是「%1」。用简体中文帮助用户处理 Telegram 对话。发送和转发必须等用户确认。"_q
					.arg(employee.name);
			}
			const auto id = employee.id;
			_storage.addOrUpdate(std::move(employee));
			_storage.setActive(id);
			_storage.save();
			_editor = false;
			_agent->loadActive();
			rebuildSettings();
			applyChrome();
			update();
		});
	} else {
		addLabel(u"xAI API 密钥"_q);
		const auto keyEdit = new QLineEdit(inner);
		keyEdit->setEchoMode(QLineEdit::Password);
		keyEdit->setText(_storage.apiKey());
		keyEdit->setPlaceholderText(u"从 console.x.ai 获取，只保存在本机"_q);
		keyEdit->setInputMethodHints(
			Qt::ImhSensitiveData
			| Qt::ImhNoAutoUppercase
			| Qt::ImhNoPredictiveText);
		layout->addWidget(keyEdit);
		addLabel(u"模型"_q);
		const auto modelEdit = new QLineEdit(inner);
		modelEdit->setText(_storage.model());
		modelEdit->setPlaceholderText(u"grok-4"_q);
		modelEdit->setInputMethodHints(
			Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText);
		layout->addWidget(modelEdit);
		const auto readBox = new QCheckBox(u"读取当前聊天"_q, inner);
		readBox->setChecked(_storage.readCurrentChat());
		layout->addWidget(readBox);
		_keyEdit = keyEdit;
		_modelEdit = modelEdit;
		_readBox = readBox;
		addLabel(u"员工"_q);
		for (const auto &employee : _storage.employees()) {
			auto *row = new QWidget(inner);
			auto *rowLayout = new QHBoxLayout(row);
			rowLayout->setContentsMargins(0, 0, 0, 0);
			rowLayout->addWidget(new QLabel(employee.name, row), 1);
			const auto edit = new QPushButton(u"编辑"_q, row);
			const auto remove = new QPushButton(u"删除"_q, row);
			rowLayout->addWidget(edit);
			rowLayout->addWidget(remove);
			layout->addWidget(row);
			const auto id = employee.id;
			QObject::connect(edit, &QPushButton::clicked, this, [=] {
				_editor = true;
				_editorId = id;
				rebuildSettings();
			});
			QObject::connect(remove, &QPushButton::clicked, this, [=] {
				if (_agent->running()) {
					_agent->pause();
				}
				_storage.remove(id);
				_storage.save();
				_agent->loadActive();
				rebuildSettings();
				applyChrome();
				update();
			});
		}
		const auto add = addButton(u"添加员工"_q);
		const auto restore = addButton(u"恢复预设"_q);
		const auto done = addButton(u"完成"_q);
		layout->addWidget(add);
		layout->addWidget(restore);
		layout->addWidget(done);
		layout->addStretch();
		QObject::connect(add, &QPushButton::clicked, this, [=] {
			_editor = true;
			_editorId = QString();
			rebuildSettings();
		});
		QObject::connect(restore, &QPushButton::clicked, this, [=] {
			_storage.restorePresets();
			_storage.save();
			rebuildSettings();
			recountHits();
			update();
		});
		QObject::connect(done, &QPushButton::clicked, this, [=] {
			closeSettings();
		});
	}
	_settingsScroll->setWidget(inner);
}

void Panel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setClipRect(e->rect());
	p.fillRect(rect(), kBg);
	if (!_open) {
		p.setPen(kText);
		p.setFont(st::semiboldFont);
		p.save();
		p.translate(width() / 2, height() / 2);
		p.rotate(-90);
		p.drawText(
			QRect(-st::grokPanelMinWidth / 2, -st::grokButtonHeight / 2, st::grokPanelMinWidth, st::grokButtonHeight),
			Qt::AlignCenter,
			u"Grok 员工"_q);
		p.restore();
		return;
	}
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	const auto logo = QRect(
		st::grokPad,
		(st::grokHeaderHeight - st::grokButtonHeight) / 2,
		st::grokButtonHeight,
		st::grokButtonHeight);
	p.drawEllipse(logo);
	p.setPen(kBg);
	p.setFont(st::semiboldFont);
	p.drawText(logo, Qt::AlignCenter, u"G"_q);
	p.setPen(kText);
	p.drawText(
		QRect(logo.right() + st::grokGap, 0, width() / 2, st::grokHeaderHeight),
		Qt::AlignVCenter | Qt::AlignLeft,
		u"Grok 员工"_q);
	for (const auto &hit : _hits) {
		if (hit.action == u"new"_q) {
			p.setPen(QPen(kLine, st::lineWidth));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(hit.rect, st::grokRadius, st::grokRadius);
			p.setPen(kText);
			p.setFont(st::normalFont);
			p.drawText(hit.rect, Qt::AlignCenter, u"新对话"_q);
		} else if (hit.action == u"settings"_q || hit.action == u"collapse"_q) {
			p.setPen(kMuted);
			p.setFont(st::semiboldFont);
			p.drawText(
				hit.rect,
				Qt::AlignCenter,
				hit.action == u"settings"_q ? u"⚙"_q : u"›"_q);
		}
	}
	for (const auto &avatar : _avatars) {
		if (avatar.selected) {
			p.setPen(QPen(kAccent, st::lineWidth * 2));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(avatar.circle.adjusted(
				-st::lineWidth * 2,
				-st::lineWidth * 2,
				st::lineWidth * 2,
				st::lineWidth * 2));
		}
		if (avatar.add) {
			p.setPen(QPen(kMuted, st::lineWidth));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(avatar.circle);
			p.setPen(kText);
			p.setFont(st::semiboldFont);
			p.drawText(avatar.circle, Qt::AlignCenter, u"+"_q);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(avatar.color));
			p.drawEllipse(avatar.circle);
			p.setPen(Qt::white);
			p.setFont(st::semiboldFont);
			p.drawText(avatar.circle, Qt::AlignCenter, avatar.name.left(1));
		}
		p.setPen(kMuted);
		p.setFont(st::normalFont);
		const auto nameRect = QRect(
			avatar.circle.x() - st::grokGap / 2,
			avatar.circle.bottom() + st::lineWidth * 2,
			avatar.circle.width() + st::grokGap,
			st::normalFont->height);
		p.drawText(
			nameRect,
			Qt::AlignHCenter | Qt::AlignTop,
			st::normalFont->elided(avatar.name, nameRect.width()));
	}
	if (!_settingsMode) {
		p.setPen(Qt::NoPen);
		p.setBrush(kAccent);
		p.drawEllipse(_sendRect);
		p.setPen(Qt::white);
		p.setFont(st::semiboldFont);
		p.drawText(_sendRect, Qt::AlignCenter, u"↑"_q);
	}
	QRect toggle;
	for (const auto &hit : _hits) {
		if (hit.action == u"toggle"_q) {
			toggle = QRect(
				st::grokPad,
				height() - st::grokFooterHeight
					+ (st::grokFooterHeight - st::grokToggleHeight) / 2,
				st::grokToggleWidth,
				st::grokToggleHeight);
		}
	}
	p.setPen(Qt::NoPen);
	p.setBrush(_storage.allowWrite() ? kAccent : kLine);
	p.drawRoundedRect(
		toggle,
		toggle.height() / 2.,
		toggle.height() / 2.);
	const auto knob = st::grokToggleHeight - st::lineWidth * 4;
	const auto knobX = _storage.allowWrite()
		? toggle.right() - knob - st::lineWidth * 2
		: toggle.left() + st::lineWidth * 2;
	p.setBrush(Qt::white);
	p.drawEllipse(knobX, toggle.top() + st::lineWidth * 2, knob, knob);
	p.setPen(kText);
	p.setFont(st::normalFont);
	const auto labelX = toggle.right() + st::grokGap;
	p.drawText(
		QRect(labelX, toggle.top() - st::lineWidth * 2, width() - labelX - st::grokPad, st::normalFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		u"允许操作 TG"_q);
	p.setPen(kMuted);
	p.setFont(st::normalFont);
	p.drawText(
		QRect(
			labelX,
			toggle.top() + st::normalFont->height,
			width() - labelX - st::grokPad,
			st::normalFont->height),
		Qt::AlignLeft | Qt::AlignTop,
		u"发送前需确认"_q);
}

Host::Host(not_null<Window::SessionController*> controller)
: _controller(controller) {
	auto panel = object_ptr<Panel>(nullptr, _controller);
	_panel = panel.data();
	_controller->widget()->setGrokPanel(std::move(panel));
	if (const auto delta = _panel->takeInitialExtend()) {
		if (!_controller->widget()->isMaximized()) {
			_controller->widget()->tryToExtendWidthBy(delta);
		}
		_controller->widget()->refreshGrokPanel();
	}
	RegisterToggle(_controller.get(), [this] { toggle(); });
}

Host::~Host() {
	RegisterToggle(_controller.get(), nullptr);
	_panel = nullptr;
	_controller->widget()->setGrokPanel(nullptr);
}

void Host::toggle() {
	if (_panel) {
		_panel->toggleOpen();
	}
}

void Toggle(not_null<Window::SessionController*> controller) {
	const auto toggle = Toggles().value(controller.get());
	if (toggle) {
		toggle();
	}
}

} // namespace Grok
