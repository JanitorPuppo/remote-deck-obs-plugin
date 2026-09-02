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
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(PluginController *controller_, QWidget *parent)
	: QDialog(parent),
	  controller(controller_)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("RemoteDeck.SettingsTitle"))
#ifdef REMOTE_DECK_LOCAL_DEV
			       + QStringLiteral(" (local dev)")
#endif
	);
	setMinimumWidth(460);

	auto *root = new QVBoxLayout(this);

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
	localApiBaseEdit->setPlaceholderText(QString::fromUtf8(kDefaultLocalRemoteDeckApiBase));
	devForm->addRow(QString::fromUtf8(obs_module_text("RemoteDeck.LocalApiBase")), localApiBaseEdit);
	root->addLayout(devForm);
	connect(localApiBaseEdit, &QLineEdit::editingFinished, this, &SettingsDialog::persistForm);
#endif

	auto *actions = new QHBoxLayout();
	authenticateBtn = new QPushButton(QString::fromUtf8(obs_module_text("RemoteDeck.Authenticate")), this);
	signOutBtn = new QPushButton(QString::fromUtf8(obs_module_text("RemoteDeck.SignOut")), this);
	actions->addWidget(authenticateBtn);
	actions->addWidget(signOutBtn);
	actions->addStretch(1);
	root->addLayout(actions);

	userCodeHint = new QLabel(QString::fromUtf8(obs_module_text("RemoteDeck.UserCodeHint")), this);
	userCodeHint->setWordWrap(true);
	userCodeHint->hide();
	root->addWidget(userCodeHint);

	userCodeLabel = new QLabel(this);
	userCodeLabel->setAlignment(Qt::AlignCenter);
	userCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	userCodeLabel->setStyleSheet(
		QStringLiteral("font-size: 22px; font-weight: 600; letter-spacing: 0.12em; padding: 8px 0;"));
	userCodeLabel->hide();
	root->addWidget(userCodeLabel);

	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	root->addWidget(statusLabel);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	root->addWidget(buttons);

	connect(authenticateBtn, &QPushButton::clicked, this, &SettingsDialog::onAuthenticate);
	connect(signOutBtn, &QPushButton::clicked, this, &SettingsDialog::onSignOut);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
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
	authenticateBtn->setText(QString::fromUtf8(
		settings.isAuthenticated() ? obs_module_text("RemoteDeck.Reauthenticate")
					   : obs_module_text("RemoteDeck.Authenticate")));
	signOutBtn->setVisible(settings.isAuthenticated());
	signOutBtn->setEnabled(!busy);

	const QString userCode = controller->userCode();
	const bool showCode = busy && !userCode.isEmpty();
	userCodeHint->setVisible(showCode);
	userCodeLabel->setVisible(showCode);
	userCodeLabel->setText(userCode);

	QString line = QString::fromUtf8(obs_module_text("RemoteDeck.Status")) + QStringLiteral(": ") +
		       controller->statusText();
	if (settings.isAuthenticated() && !settings.studioName.isEmpty())
		line = QString::fromUtf8(obs_module_text("RemoteDeck.SignedIn")).arg(settings.studioName) +
		       QStringLiteral("\n") + line;
	statusLabel->setText(line);
}

void SettingsDialog::onAuthenticate()
{
	persistForm();
	controller->authenticate();
}

void SettingsDialog::onSignOut()
{
	controller->signOut();
	reload();
}
