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

#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;
class PluginController;

class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(PluginController *controller, QWidget *parent = nullptr);

	void reload();

private:
	void persistForm();
	void refreshStatus();
	void onAuthenticate();
	void onCancelAuthentication();
	void onSignOut();

	PluginController *controller = nullptr;
	QLineEdit *machineLabelEdit = nullptr;
#ifdef REMOTE_DECK_LOCAL_DEV
	QLineEdit *localApiBaseEdit = nullptr;
#endif
	QPushButton *authenticateBtn = nullptr;
	QPushButton *cancelAuthBtn = nullptr;
	QPushButton *signOutBtn = nullptr;
	QLabel *userCodeHint = nullptr;
	QLabel *userCodeLabel = nullptr;
	QLabel *statusLabel = nullptr;
	QWidget *codePanel = nullptr;
};
