/*
obs-remote-deck
Copyright (C) 2026 Remote Deck

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "settings-dialog.hpp"
#include "plugin-controller.hpp"

#include <obs-module.h>

#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

SettingsDialog::SettingsDialog(PluginController *controller_, QWidget *parent)
	: QDialog(parent),
	  controller(controller_)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("RemoteDeck.SettingsTitle"))
#ifdef REMOTE_DECK_LOCAL_DEV
			       + QStringLiteral(" (local dev)")
#endif
	);
	setMinimumWidth(480);
	setMinimumHeight(360);

	auto *root = new QVBoxLayout(this);
	root->setSpacing(10);
	root->setContentsMargins(16, 16, 16, 16);

	auto *form = new QFormLayout();
	machineLabelEdit = new QLineEdit(this);
	machineLabelEdit->setPlaceholderText(QString::fromUtf8(obs_module_text("RemoteDeck.MachinePlaceholder")));
	form->addRow(QString::fromUtf8(obs_module_text("RemoteDeck.MachineLabel")), machineLabelEdit);
	root->addLayout(form);

	auto *intro = new QLabel(QString::fromUtf8(obs_module_text("RemoteDeck.Intro")), this);
	intro->setWordWrap(true);
	root->addWidget(intro);

#ifdef REMOTE_DECK_LOCAL_DEV
	auto *devForm = new QFormLayout();
	localApiBaseEdit = new QLineEdit(this);
	localApiBaseEdit->setPlaceholderText(QString::fromUtf8(kDefaultLocalRemoteDeckApiBase)
						   + QStringLiteral(" or https://www.remotedeck.gg"));
	devForm->addRow(QString::fromUtf8(obs_module_text("RemoteDeck.LocalApiBase")), localApiBaseEdit);
	root->addLayout(devForm);
	connect(localApiBaseEdit, &QLineEdit::editingFinished, this, &SettingsDialog::persistForm);
#endif

	authenticateBtn = new QPushButton(QString::fromUtf8(obs_module_text("RemoteDeck.Authenticate")), this);
	auto *authRow = new QHBoxLayout();
	authRow->addWidget(authenticateBtn);
	authRow->addStretch(1);
	root->addLayout(authRow);

	codePanel = new QWidget(this);
	auto *codeLayout = new QVBoxLayout(codePanel);
	codeLayout->setContentsMargins(0, 12, 0, 12);
	codeLayout->setSpacing(16);
	codeLayout->addStretch(1);

	userCodeHint = new QLabel(QString::fromUtf8(obs_module_text("RemoteDeck.UserCodeHint")), codePanel);
	userCodeHint->setWordWrap(true);
	userCodeHint->setAlignment(Qt::AlignCenter);
	userCodeHint->setMinimumWidth(360);
	codeLayout->addWidget(userCodeHint, 0, Qt::AlignHCenter);

	userCodeLabel = new QLabel(codePanel);
	userCodeLabel->setAlignment(Qt::AlignCenter);
	userCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont codeFont = userCodeLabel->font();
	codeFont.setPointSize(28);
	codeFont.setWeight(QFont::DemiBold);
	codeFont.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
	userCodeLabel->setFont(codeFont);
	userCodeLabel->setMinimumHeight(QFontMetrics(codeFont).height() + 16);
	userCodeLabel->setContentsMargins(0, 8, 0, 8);
	codeLayout->addWidget(userCodeLabel, 0, Qt::AlignHCenter);

	codeLayout->addStretch(1);
	codePanel->setMinimumHeight(140);
	codePanel->hide();
	root->addWidget(codePanel, 1);

	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	root->addWidget(statusLabel);

	cancelAuthBtn = new QPushButton(QString::fromUtf8(obs_module_text("RemoteDeck.CancelAuth")), this);
	signOutBtn = new QPushButton(QString::fromUtf8(obs_module_text("RemoteDeck.SignOut")), this);
	cancelAuthBtn->hide();
	signOutBtn->hide();

	auto *bottomActions = new QHBoxLayout();
	bottomActions->setContentsMargins(0, 4, 0, 0);
	bottomActions->addWidget(cancelAuthBtn);
	bottomActions->addWidget(signOutBtn);
	bottomActions->addStretch(1);
	root->addLayout(bottomActions);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	root->addWidget(buttons);

	connect(authenticateBtn, &QPushButton::clicked, this, &SettingsDialog::onAuthenticate);
	connect(cancelAuthBtn, &QPushButton::clicked, this, &SettingsDialog::onCancelAuthentication);
	connect(signOutBtn, &QPushButton::clicked, this, &SettingsDialog::onSignOut);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	connect(this, &QDialog::finished, this, [this](int) {
		if (controller->isAuthenticating())
			controller->cancelAuthentication();
	});
	connect(machineLabelEdit, &QLineEdit::editingFinished, this, &SettingsDialog::persistForm);
	connect(controller, &PluginController::statusChanged, this, &SettingsDialog::refreshStatus);

	reload();
}

void SettingsDialog::reload()
{
	const PluginSettings settings = controller->settings();
	machineLabelEdit->setText(settings.machineLabel);
#ifdef REMOTE_DECK_LOCAL_DEV
	if (localApiBaseEdit)
		localApiBaseEdit->setText(settings.remoteDeckApiBase);
#endif
	refreshStatus();
}

void SettingsDialog::persistForm()
{
	PluginSettings settings = controller->settings();
	settings.machineLabel = machineLabelEdit->text().trimmed();
#ifdef REMOTE_DECK_LOCAL_DEV
	if (localApiBaseEdit)
		settings.remoteDeckApiBase = localApiBaseEdit->text().trimmed();
#endif
	controller->saveSettings(settings);
}

void SettingsDialog::refreshStatus()
{
	const PluginSettings settings = controller->settings();
	const bool busy = controller->isAuthenticating();

	authenticateBtn->setEnabled(!busy);
	authenticateBtn->setVisible(!busy);
	authenticateBtn->setText(QString::fromUtf8(
		settings.isAuthenticated() ? obs_module_text("RemoteDeck.Reauthenticate")
					   : obs_module_text("RemoteDeck.Authenticate")));
	cancelAuthBtn->setVisible(busy);
	signOutBtn->setVisible(settings.isAuthenticated());
	signOutBtn->setEnabled(!busy);

	const QString userCode = controller->userCode();
	const bool showCode = busy && !userCode.isEmpty();
	codePanel->setVisible(showCode);
	userCodeLabel->setText(userCode);

	statusLabel->setVisible(!showCode);
	if (!showCode) {
		QString line = QString::fromUtf8(obs_module_text("RemoteDeck.Status")) + QStringLiteral(": ") +
			       controller->statusText();
		if (settings.isAuthenticated() && !settings.studioName.isEmpty())
			line = QString::fromUtf8(obs_module_text("RemoteDeck.SignedIn")).arg(settings.studioName) +
			       QStringLiteral("\n") + line;
		statusLabel->setText(line);
	}

	if (showCode)
		setMinimumHeight(400);
	else
		setMinimumHeight(360);
}

void SettingsDialog::onAuthenticate()
{
	persistForm();
	controller->authenticate();
}

void SettingsDialog::onCancelAuthentication()
{
	controller->cancelAuthentication();
	refreshStatus();
}

void SettingsDialog::onSignOut()
{
	controller->signOut();
	reload();
}
