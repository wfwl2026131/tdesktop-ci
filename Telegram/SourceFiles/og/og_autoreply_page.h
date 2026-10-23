/*
This file is part of an Officegram project, a Telegram Desktop fork
for marketing CRM workflows.

For license and copyright information of the underlying Telegram Desktop
code please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/required.h"
#include "base/weak_ptr.h"
#include "og/og_store.h"

#include <QtWidgets/QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace Ui::Toast {
class Instance;
} // namespace Ui::Toast

namespace Window {
class SessionController;
} // namespace Window

namespace OG {

// The settings page of the auto-reply feature.
//
// Layout follows the reference: one switch for the whole feature, one field
// for the duplicate window, then the rules. Every rule is one hand built row
// (keyword, reply, delete), the same shape as the todo and client pages use,
// so no table widget is involved.
//
// The page talks to the store only. The controller in the signature is there
// for parity with the other pages and for the entry point that is still being
// decided; nothing here holds it.
class AutoReplyPageWindow final : public QDialog {
public:
	AutoReplyPageWindow(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		bool embedded = false);

	// Escape only hides the window, the next call reuses the instance; the
	// title bar button destroys it, exactly like the other pages.
	static void Show(not_null<Window::SessionController*> controller);

	static constexpr auto kWidth = 900;
	static constexpr auto kHeight = 640;

private:
	void setupContent();
	void rebuild();

	void addRuleRow(const AutoReplyRule &entry);
	void addRule();
	void removeRule(qint64 id);
	void saveSettings();

	// Only one toast is alive at a time: the next one takes the previous one
	// down first, the way the window-level helper does.
	void showToast(const QString &text);

	void reject() override;

	QVBoxLayout *_rows = nullptr;
	QWidget *_rowsHost = nullptr;
	QCheckBox *_enabled = nullptr;
	QSpinBox *_duplicate = nullptr;
	QLineEdit *_keyword = nullptr;
	QLineEdit *_reply = nullptr;

	base::weak_ptr<Ui::Toast::Instance> _lastToast;
};

} // namespace OG
