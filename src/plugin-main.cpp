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

#include "plugin-controller.hpp"
#include "qt-tls-setup.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("RemoteDeck");
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("RemoteDeck.Description");
}

static PluginController *controller = nullptr;

static void on_tools_menu(void *)
{
	if (controller)
		controller->showSettings();
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (!controller)
		return;
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		controller->start();
	} else if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED ||
		   event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED ||
		   event == OBS_FRONTEND_EVENT_SCENE_CHANGED) {
		controller->refreshSources();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		controller->stop();
	}
}

bool obs_module_load(void)
{
	if (!ensureQtTlsBackend())
		obs_log(LOG_WARNING, "HTTPS/WSS will fail until Qt TLS backends are installed with the plugin");

	controller = new PluginController();
	if (!controller->init()) {
		delete controller;
		controller = nullptr;
		return false;
	}

	obs_frontend_add_tools_menu_item(obs_module_text("RemoteDeck.Menu"), on_tools_menu, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
#ifdef REMOTE_DECK_LOCAL_DEV
	obs_log(LOG_WARNING, "LOCAL DEV build: local Remote Deck URL override is enabled; do not ship this DLL");
#endif
	return true;
}

void obs_module_unload(void)
{
	if (controller) {
		obs_frontend_remove_event_callback(on_frontend_event, nullptr);
		delete controller;
		controller = nullptr;
	}
	obs_log(LOG_INFO, "plugin unloaded");
}
