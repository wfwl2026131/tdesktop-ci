/*
This file is part of an Officegram project, a Telegram Desktop fork
for marketing CRM workflows.

For license and copyright information of the underlying Telegram Desktop
code please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "og/og_autoreply_page.h"

#include "og/og_tr.h"
#include "ui/toast/toast.h"

#include <QtCore/QPointer>

#include <QtGui/QFont>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>

namespace OG {
namespace {

constexpr auto kRowMargin = 10;
constexpr auto kReplyWidth = 460;

// The rows are rebuilt from the store after every action, and a row destroys
// itself from the click handler of a button that lives inside it: the widgets
// are hidden first and deleted once the event loop returns.
void ClearLayout(QVBoxLayout *layout) {
	while (auto *item = layout->takeAt(0)) {
		if (auto *widget = item->widget()) {
			widget->hide();
			widget->deleteLater();
		}
		delete item;
	}
}

} // namespace

AutoReplyPageWindow::AutoReplyPageWindow(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	bool embedded)
: QDialog(parent) {
	// The controller is part of the signature for parity with the other
	// pages; the page itself talks to the store only.
	Q_UNUSED(controller);

	// Embedded mode is the console page stack: the title, the window flags and
	// the fixed size belong to a top level page, and a page that lives inside
	// the stack must not carry any of them. QDialog is a window by default, so
	// the flags are cleared to a plain child widget instead of leaving the
	// question to the reparenting rules of the stack.
	if (!embedded) {
		setWindowTitle(Tr::Lookup("AutoReplyPage::Title"));
		setWindowFlags(
			Qt::Dialog
			| Qt::WindowTitleHint
			| Qt::WindowSystemMenuHint
			| Qt::WindowCloseButtonHint);
		resize(kWidth, kHeight);
	} else {
		setWindowFlags(Qt::Widget);
	}
	setupContent();
	rebuild();
}

void AutoReplyPageWindow::Show(
		not_null<Window::SessionController*> controller) {
	static auto instance = QPointer<AutoReplyPageWindow>();
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
	const auto window = new AutoReplyPageWindow(nullptr, controller);
	window->setAttribute(Qt::WA_DeleteOnClose);
	instance = window;
	window->show();
	window->raise();
	window->activateWindow();
}

void AutoReplyPageWindow::reject() {
	// Escape hides the window instead of destroying it, so the next Show()
	// brings back the same instance and the half typed rule survives.
	hide();
}

void AutoReplyPageWindow::setupContent() {
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(kRowMargin, kRowMargin, kRowMargin, kRowMargin);

	auto *settings = new QHBoxLayout();
	settings->setSpacing(8);
	_enabled = new QCheckBox(Tr::Lookup("AutoReplyPage::Enabled"), this);
	settings->addWidget(_enabled);

	settings->addSpacing(16);
	settings->addWidget(new QLabel(
		Tr::Lookup("AutoReplyPage::Duplicate Cache"),
		this));
	_duplicate = new QSpinBox(this);
	_duplicate->setRange(0, 86400);
	_duplicate->setSingleStep(5);
	settings->addWidget(_duplicate);
	settings->addStretch(1);
	layout->addLayout(settings);

	connect(_enabled, &QCheckBox::toggled, this, [this] {
		saveSettings();
	});
	connect(
		_duplicate,
		QOverload<int>::of(&QSpinBox::valueChanged),
		this,
		[this] {
			saveSettings();
		});

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	layout->addWidget(scroll, 1);

	_rowsHost = new QWidget();
	_rows = new QVBoxLayout(_rowsHost);
	_rows->setContentsMargins(0, 0, 0, 0);
	_rows->setSpacing(2);
	_rows->setAlignment(Qt::AlignTop);
	scroll->setWidget(_rowsHost);

	auto *add = new QHBoxLayout();
	add->setSpacing(8);
	_keyword = new QLineEdit(this);
	_keyword->setPlaceholderText(Tr::Lookup("AutoReplyPage::Keyword"));
	add->addWidget(_keyword, 1);

	_reply = new QLineEdit(this);
	_reply->setPlaceholderText(Tr::Lookup("AutoReplyPage::Reply Content"));
	add->addWidget(_reply, 2);

	auto *button = new QPushButton(Tr::Lookup("AutoReplyPage::Add Rule"), this);
	add->addWidget(button);
	layout->addLayout(add);

	connect(button, &QPushButton::clicked, this, [this] {
		addRule();
	});
}

void AutoReplyPageWindow::rebuild() {
	const auto settings = StoreInstance().autoReplySettings();
	// Both setters are wrapped, the check box included: setChecked() emits
	// toggled() when the state really changes, and that handler would save the
	// settings before the spin box below has been given its stored value.
	_enabled->blockSignals(true);
	_duplicate->blockSignals(true);
	_enabled->setChecked(settings.enabled);
	_duplicate->setValue(settings.duplicateSeconds);
	_duplicate->blockSignals(false);
	_enabled->blockSignals(false);

	ClearLayout(_rows);

	const auto &rules = StoreInstance().autoReplyRules();
	for (const auto &rule : rules) {
		addRuleRow(rule);
	}
	if (rules.empty()) {
		auto *empty = new QLabel(Tr::Lookup("AutoReplyPage::Empty"), _rowsHost);
		empty->setAlignment(Qt::AlignCenter);
		_rows->addWidget(empty);
	}
	// The trailing stretch keeps the rows at the top instead of letting the
	// labels share out the free height.
	_rows->addStretch(1);
}

void AutoReplyPageWindow::addRuleRow(const AutoReplyRule &entry) {
	auto *row = new QWidget(_rowsHost);
	auto *line = new QHBoxLayout(row);
	line->setContentsMargins(0, 6, 0, 6);
	line->setSpacing(8);

	auto *keyword = new QLabel(row);
	keyword->setText(entry.keyword);
	auto bold = keyword->font();
	bold.setBold(true);
	keyword->setFont(bold);
	line->addWidget(keyword, 1);

	auto *reply = new QLabel(row);
	reply->setText(entry.reply);
	reply->setMinimumWidth(kReplyWidth);
	reply->setWordWrap(true);
	line->addWidget(reply, 2);

	const auto id = entry.id;
	auto *remove = new QPushButton(Tr::Lookup("AutoReplyPage::Delete"), row);
	line->addWidget(remove);
	connect(remove, &QPushButton::clicked, this, [this, id] {
		removeRule(id);
	});

	_rows->addWidget(row);
}

void AutoReplyPageWindow::addRule() {
	const auto keyword = _keyword->text().trimmed();
	const auto reply = _reply->text().trimmed();
	if (keyword.isEmpty() || reply.isEmpty()) {
		showToast(Tr::Lookup("AutoReplyPage::Both Required"));
		return;
	}
	const auto id = StoreInstance().addAutoReplyRule(keyword, reply);
	if (!id) {
		showToast(Tr::Lookup("AutoReplyPage::Both Required"));
		return;
	}
	_keyword->clear();
	_reply->clear();
	rebuild();
	showToast(
		Tr::Lookup("AutoReplyPage::Added").arg(keyword));
}

void AutoReplyPageWindow::removeRule(qint64 id) {
	const auto entry = StoreInstance().autoReplyRule(id);
	if (!entry) {
		// The row went away between the click and here: rebuild anyway, so
		// the view cannot keep showing a rule the store no longer has.
		rebuild();
		return;
	}
	const auto keyword = entry->keyword;
	if (!StoreInstance().removeAutoReplyRule(id)) {
		rebuild();
		return;
	}
	rebuild();
	showToast(Tr::Lookup("AutoReplyPage::Deleted").arg(keyword));
}

void AutoReplyPageWindow::saveSettings() {
	auto settings = AutoReplySettings();
	settings.enabled = _enabled->isChecked();
	settings.duplicateSeconds = _duplicate->value();
	StoreInstance().setAutoReplySettings(settings);
}

void AutoReplyPageWindow::showToast(const QString &text) {
	if (const auto strong = _lastToast.get()) {
		strong->hideAnimated();
	}
	// The parent is this window, so the toast is centred in it and stays
	// visible while the page is the active window.
	_lastToast = Ui::Toast::Show(this, Ui::Toast::Config{
		.text = TextWithEntities{ text },
		.duration = 1500,
	});
}

} // namespace OG
