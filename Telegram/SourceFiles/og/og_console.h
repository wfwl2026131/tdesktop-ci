/*
This file is part of an Officegram project, a Telegram Desktop fork
for marketing CRM workflows.

For license and copyright information of the underlying Telegram Desktop
code please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/required.h"

#include <QtWidgets/QDialog>

#include <memory>

class QShowEvent;

namespace style {
struct ScrollArea;
} // namespace style

namespace Ui {

class VerticalLayout;

} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Deep {

// Console window of the reference client, reproduced for our fork.
//
// Evidence: `Deep::ConsoleWindowQt` inherits QDialog in the reference binary,
// so this is a real top-level window and not a box layer. The client area is
// split into a fixed navigation column and a page stack: the reference window
// measures 140 and 1052 logical pixels wide inside its 1210 pixel client area,
// and switching a navigation entry swaps the whole right pane - there is no
// nested tab strip and no second window.
class ConsoleWindowQt final : public QDialog {
public:
	ConsoleWindowQt(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	// Defined in the source file, where the scroll area style is complete.
	~ConsoleWindowQt() override;

	// Shows the window and brings it to the front. Escape only hides it and the
	// next call reuses the same instance; closing it (the title bar button)
	// destroys it and the call after that opens a fresh one.
	static void Show(not_null<Window::SessionController*> controller);

	// Client area of the reference window (Windows: 1210x770 measured with
	// GetClientRect, while the whole window is 1223x806 because the 29 logical
	// px title bar sits outside of it).
	static constexpr auto kWidth = 1210;
	static constexpr auto kHeight = 770;

private:
	// Navigation column state: the buttons of the left list and the page stack
	// they drive. Defined in the source file, because both need the full widget
	// headers and nothing outside of this window touches them.
	struct Nav;

	void setupContent();
	void buildNav(not_null<Ui::VerticalLayout*> nav);
	void select(int index);

	// Re-applies the navigation geometry: the scroll bar of the column and the
	// scroll range of its wheel are both computed before the window has a
	// size, so they are recomputed once it is on screen.
	void refreshNav();

	void showEvent(QShowEvent *e) override;

	// Escape hides the window instead of closing it, so that the instance
	// survives for the next Show() to reuse.
	void reject() override;

	const not_null<Window::SessionController*> _controller;

	// The scroll area keeps a reference to the style it was built with, so the
	// window owns the copy instead of handing it a temporary.
	const std::unique_ptr<style::ScrollArea> _scrollStyle;

	const std::unique_ptr<Nav> _nav;
};

} // namespace Deep
