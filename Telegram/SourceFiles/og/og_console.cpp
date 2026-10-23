/*
This file is part of an Officegram project, a Telegram Desktop fork
for marketing CRM workflows.

For license and copyright information of the underlying Telegram Desktop
code please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "og/og_console.h"

#include "base/object_ptr.h"
#include "og/og_autoreply_page.h"
#include "og/og_tr.h"
#include <rpl/producer.h>
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>

#include <optional>
#include <vector>

namespace Deep {
namespace {

// Geometry of the reference window: the navigation column is 140 logical
// pixels wide and the page stack 1052, which leaves 9 pixels of margin on each
// side of the 1210 pixel client area.
constexpr auto kSideMargin = 9;
constexpr auto kNavWidth = 140;
constexpr auto kStackWidth = 1052;

// The reference measures its console bar at 17 logical pixels with a 12 pixel
// wide handle, 2.5 pixels off each edge (bar rect 278,253 17x758; handle 12).
// Ui::ScrollArea paints that bar on top of the viewport and never reserves
// room for it, so the column content stops before the bar instead: 140 - 17 =
// 123 pixels, which is what the reference viewport shows as well.
constexpr auto kBarWidth = 17;
constexpr auto kNavContentWidth = kNavWidth - kBarWidth;

// The console keeps its own scroll area style, so the column carries a bar at
// all times (see the constructor); the bars of every other window keep the
// default behaviour.

constexpr auto kTitleKey = "ConsoleWindowQt::Title";
constexpr auto kNotImplementedKey = "ConsolePlaceholder::Not Implemented";

// The navigation column of the reference window, in its own order. The emoji
// that prefixes every label lives in the translation file, not here: these are
// the English source strings the reference resources carry.
constexpr const char *kNavKeys[] = {
	"ConsoleNav::Puppet Chat",
	"ConsoleNav::Copy Chat",
	"ConsoleNav::AI Original Chat",
	"ConsoleNav::AI Comment",
	"ConsoleNav::AI Channel Repost",
	"ConsoleNav::Mass Send",
	"ConsoleNav::Auto Reply",
	"ConsoleNav::Watch Chat",
	"ConsoleNav::Batch Join",
	"ConsoleNav::Batch Login",
	"ConsoleNav::Batch Rename",
	"ConsoleNav::Batch Invite",
	"ConsoleNav::Filter Phone",
	"ConsoleNav::Filter Group",
	"ConsoleNav::Video Manage",
	"ConsoleNav::Proxy Config",
	"ConsoleNav::Batch Contacts",
	"ConsoleNav::Batch Invite v2",
	"ConsoleNav::Mass DM v2",
	"ConsoleNav::Puppet Chat v2",
	"ConsoleNav::Auto Marketing",
	"ConsoleNav::Keep Online",
	"ConsoleNav::Advanced Features",
	"ConsoleNav::Auto Script",
	"ConsoleNav::Settings",
};

constexpr auto kNavCount = int(sizeof(kNavKeys) / sizeof(kNavKeys[0]));

// The only entry that is already built: the auto-reply page of the previous
// milestone. Everything else is a placeholder until its own ticket lands.
constexpr auto kAutoReplyIndex = 6;

// Placeholder body of a navigation entry: the entry name and the one line that
// says the feature is not built yet. No invented form and no fake buttons.
//
// QLabel, not Ui::FlatLabel: the latter is an RpWidget that sizes itself only
// through resizeGetHeight() and has no size hint, so a plain QVBoxLayout gives
// it a width of zero, its height collapses and nothing is painted or exposed
// to the accessibility tree. QLabel has a real size hint, and the acceptance
// run reads the page by its text.
QWidget *MakePlaceholderPage(QWidget *parent, const char *navKey) {
	const auto page = new QWidget(parent);
	const auto layout = new QVBoxLayout(page);
	layout->setContentsMargins(24, 24, 24, 24);
	layout->setSpacing(8);

	const auto title = new QLabel(OG::Tr::Lookup(navKey), page);
	auto titleFont = title->font();
	if (titleFont.pointSizeF() > 0) {
		titleFont.setPointSizeF(titleFont.pointSizeF() + 5);
	}
	titleFont.setBold(true);
	title->setFont(titleFont);
	layout->addWidget(title);

	const auto body = new QLabel(OG::Tr::Lookup(kNotImplementedKey), page);
	auto bodyFont = body->font();
	if (bodyFont.pointSizeF() > 1.) {
		bodyFont.setPointSizeF(bodyFont.pointSizeF() - 1);
	}
	body->setFont(bodyFont);
	layout->addWidget(body);

	layout->addStretch(1);
	return page;
}

} // namespace

struct ConsoleWindowQt::Nav {
	QStackedWidget *stack = nullptr;
	QPointer<Ui::ScrollArea> scroll;
	QPointer<Ui::VerticalLayout> content;
	std::vector<not_null<Ui::SettingsButton*>> buttons;
	int current = 0;
};

ConsoleWindowQt::ConsoleWindowQt(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: QDialog(parent)
, _controller(controller)
, _scrollStyle([] {
	auto result = std::make_unique<style::ScrollArea>(st::defaultScrollArea);
	// Bar geometry of the reference console: 17 pixels of track with a handle
	// that keeps 2 pixels off each edge, so width() - 2 * deltax = 13 against
	// the measured 12. The reference offset is 2.5 and integer style fields
	// cannot hold it; rounding down keeps the handle a pixel wider than
	// rounding up would (11), and the wider handle matches the grab area of
	// the reference bar more closely than a thinner one.
	result->width = kBarWidth;
	result->deltax = 2;
	// The reference console keeps its bar on screen and never fades it out,
	// while st::defaultScrollArea starts a hide timer after every scroll. A
	// zero interval means the timer is never armed: every arm site in
	// scroll_area.cpp sits behind `if (_st->hiding)` (:89 for the initial
	// _hiding flag, :288, :349, :477, :488, :781, :850 and :858), so this
	// window only turns off the fade.
	//
	// Two separate switches, do not confuse them: `hiding` controls the fade
	// of a bar that is on screen, while `barHidden` decides whether a bar
	// exists at all - it is the first condition of ScrollBar::updateBar()
	// (scroll_area.cpp:151). A style that carries `barHidden: true` would
	// still show no bar with `hiding` set to zero.
	result->hiding = 0;
	return result;
}())
, _nav(std::make_unique<Nav>()) {
	setWindowTitle(OG::Tr::Lookup(kTitleKey));
	// The reference window cannot be resized and its title bar carries a single
	// button, so the flags are spelled out: no context help question mark (a
	// QDialog adds it by default), and MSWindowsFixedSizeDialogHint instead of
	// relying on Qt to drop WS_THICKFRAME.
	setWindowFlags(
		Qt::Dialog
		| Qt::WindowTitleHint
		| Qt::WindowSystemMenuHint
		| Qt::WindowCloseButtonHint
		| Qt::MSWindowsFixedSizeDialogHint);
	setFixedSize(kWidth, kHeight);
	setupContent();
}

ConsoleWindowQt::~ConsoleWindowQt() = default;

void ConsoleWindowQt::Show(
		not_null<Window::SessionController*> controller) {
	// One instance is kept alive while the window is only hidden, so that
	// Escape and a second menu click bring back the very same window. Closing
	// it deletes it and clears this pointer, which makes the next call open a
	// fresh window again.
	static auto instance = QPointer<ConsoleWindowQt>();
	if (instance) {
		if (instance->windowState() & Qt::WindowMinimized) {
			instance->setWindowState(
				(instance->windowState() & ~Qt::WindowMinimized)
				| Qt::WindowActive);
		}
		instance->show();
		instance->raise();
		instance->activateWindow();
		return;
	}
	const auto window = new ConsoleWindowQt(nullptr, controller);
	window->setAttribute(Qt::WA_DeleteOnClose);
	instance = window;
	window->show();
	window->raise();
	window->activateWindow();
}

void ConsoleWindowQt::reject() {
	// Escape hides the window instead of closing it: the reference client keeps
	// the same native window and reuses it, and only its X button destroys it.
	hide();
}

void ConsoleWindowQt::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	// Both numbers of the column are computed before the window has a size:
	// the bar of a Ui::ScrollArea is placed in the resize event of the area,
	// and the range Qt keeps for the wheel comes from the content widget at
	// the moment it enters the area. Neither has run with the real geometry
	// yet, so the bar stayed hidden (updateBar() saw a zero height) and the
	// wheel had nothing to move. Redo it here, once the layout is applied,
	// and once more from the event loop in case the layout pass lands later.
	refreshNav();
	QTimer::singleShot(0, this, [=] { refreshNav(); });
}

void ConsoleWindowQt::refreshNav() {
	const auto scroll = _nav->scroll.data();
	if (!scroll) {
		return;
	}
	if (const auto content = _nav->content.data()) {
		content->resizeToWidth(kNavContentWidth);
	}

	// The area computes its range from the content height itself every time it
	// paints, so its number is the truth for this window; the Qt bar is what
	// the wheel moves and what updateBar() measures, and it was left with the
	// range of an empty area. Handing the same number over fixes the wheel and
	// makes the range change run updateBar() with the real bar geometry.
	const auto top = scroll->scrollTopMax();
	if (scroll->verticalScrollBar()->maximum() != top) {
		scroll->verticalScrollBar()->setMaximum(top);
	}
	scroll->updateBars();
}

void ConsoleWindowQt::setupContent() {
	const auto layout = new QHBoxLayout(this);
	layout->setContentsMargins(kSideMargin, 0, kSideMargin, 0);
	layout->setSpacing(0);

	// Ui::VerticalLayout has no size hint at all: its height comes out of
	// resizeGetHeight(), which only runs when a parent passes a width in. A
	// bare QScrollArea with setWidgetResizable(true) sizes its content from
	// sizeHint() instead, so the layout was squashed to the viewport, the rows
	// past its bottom edge were clipped and the scroll range stayed zero.
	// Ui::ScrollArea keeps the widget at the size the widget itself sets.
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(this, *_scrollStyle);
	scroll->setFixedWidth(kNavWidth);

	auto nav = object_ptr<Ui::VerticalLayout>(scroll);
	const auto navRaw = nav.data();
	buildNav(navRaw);

	// The width is applied before the ownership moves into the scroll area:
	// setWidget() resizes a widget that was never resized to its size hint,
	// and this one only has a size once resizeToWidth() has run. The rows get
	// the viewport width, so the bar does not paint over their right edge.
	navRaw->resizeToWidth(kNavContentWidth);
	scroll->setOwnedWidget(std::move(nav));
	// Applied a second time now that the widget lives in the viewport: the
	// area installs its event filter on the widget in setWidget(), and only
	// from then on does a resize of the content update the range Qt keeps for
	// the wheel.
	navRaw->resizeToWidth(kNavContentWidth);
	layout->addWidget(scroll);

	_nav->scroll = scroll;
	_nav->content = navRaw;

	const auto stack = new QStackedWidget(this);
	stack->setFixedWidth(kStackWidth);
	for (auto i = 0; i != kNavCount; ++i) {
		stack->addWidget((i == kAutoReplyIndex)
			? static_cast<QWidget*>(new OG::AutoReplyPageWindow(
				stack,
				_controller,
				true))
			: MakePlaceholderPage(stack, kNavKeys[i]));
	}
	layout->addWidget(stack);

	_nav->stack = stack;
	select(_nav->current);
}

void ConsoleWindowQt::buildNav(not_null<Ui::VerticalLayout*> nav) {
	_nav->buttons.clear();
	_nav->buttons.reserve(kNavCount);
	for (auto i = 0; i != kNavCount; ++i) {
		// No margin around a row: st::settingsButtonNoIcon already carries the
		// vertical padding of the reference row (10 above, 8 below, about 38
		// logical pixels with the console font), and the reference stacks its
		// 25 entries with no gap between them: the measured scroll range of
		// 204 pixels over a 758 pixel column fits 25 rows of that height.
		const auto button = nav->add(
			object_ptr<Ui::SettingsButton>(
				nav,
				rpl::single(OG::Tr::Lookup(kNavKeys[i])),
				st::settingsButtonNoIcon));
		button->setClickedCallback([=] {
			select(i);
		});
		_nav->buttons.push_back(button);
	}
}

void ConsoleWindowQt::select(int index) {
	if (index < 0 || index >= kNavCount) {
		return;
	}
	_nav->current = index;
	if (const auto stack = _nav->stack) {
		stack->setCurrentIndex(index);
	}
	for (auto i = 0; i != int(_nav->buttons.size()); ++i) {
		_nav->buttons[i]->setColorOverride(
			(i == index)
				? std::optional<QColor>(st::windowActiveTextFg->c)
				: std::nullopt);
	}
}

} // namespace Deep
