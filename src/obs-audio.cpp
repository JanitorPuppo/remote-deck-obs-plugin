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

#include "obs-audio.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QMetaObject>
#include <QPointer>

namespace {

constexpr double kMinVolumeDb = -100.0;
constexpr double kMaxVolumeDb = 26.0;

double volumeToDb(float mul)
{
	if (mul <= 0.0f)
		return kMinVolumeDb;
	return static_cast<double>(obs_mul_to_db(mul));
}

float dbToVolume(double db)
{
	if (db <= kMinVolumeDb)
		return 0.0f;
	return obs_db_to_mul(static_cast<float>(db));
}

}

ObsAudio::ObsAudio(QObject *parent) : QObject(parent) {}

ObsAudio::~ObsAudio()
{
	stop();
}

bool ObsAudio::isAudioInput(obs_source_t *source)
{
	if (!source)
		return false;
	if (obs_source_get_type(source) != OBS_SOURCE_TYPE_INPUT)
		return false;
	return (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

struct SceneMembershipWalk {
	QString topSceneName;
	QSet<QString> walking;
	QHash<QString, QStringList> *membership = nullptr;
};

QJsonObject ObsAudio::describeSource(obs_source_t *source, const QHash<QString, QStringList> *membership)
{
	QJsonObject obj;
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	obj.insert("id", id);
	obj.insert("name", QString::fromUtf8(name ? name : ""));
	obj.insert("muted", obs_source_muted(source));
	obj.insert("volumeDb", volumeToDb(obs_source_get_volume(source)));
	if (membership && !id.isEmpty()) {
		const QStringList sceneNames = membership->value(id);
		if (!sceneNames.isEmpty()) {
			QJsonArray names;
			for (const QString &sceneName : sceneNames)
				names.append(sceneName);
			obj.insert("sceneNames", names);
		}
	}
	return obj;
}

bool ObsAudio::enumSceneItems(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	auto *ctx = static_cast<SceneMembershipWalk *>(param);
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src || !ctx || !ctx->membership)
		return true;

	if (isAudioInput(src)) {
		const char *uuid = obs_source_get_uuid(src);
		const QString id = QString::fromUtf8(uuid ? uuid : "");
		if (!id.isEmpty() && !ctx->membership->value(id).contains(ctx->topSceneName))
			(*ctx->membership)[id].append(ctx->topSceneName);
	}

	if (obs_sceneitem_is_group(item)) {
		obs_sceneitem_group_enum_items(item, enumSceneItems, param);
		return true;
	}

	if (obs_source_get_type(src) != OBS_SOURCE_TYPE_SCENE)
		return true;

	const char *uuid = obs_source_get_uuid(src);
	const QString nestedId = QString::fromUtf8(uuid ? uuid : "");
	obs_scene_t *nested = obs_scene_from_source(src);
	if (!nested || nestedId.isEmpty() || ctx->walking.contains(nestedId))
		return true;
	ctx->walking.insert(nestedId);
	obs_scene_enum_items(nested, enumSceneItems, param);
	ctx->walking.remove(nestedId);
	return true;
}

QHash<QString, QStringList> ObsAudio::buildSceneMembership()
{
	QHash<QString, QStringList> membership;
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *sceneSource = list.sources.array[i];
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		const char *name = obs_source_get_name(sceneSource);
		if (!scene || !name || !name[0])
			continue;
		SceneMembershipWalk ctx;
		ctx.topSceneName = QString::fromUtf8(name);
		ctx.membership = &membership;
		const char *uuid = obs_source_get_uuid(sceneSource);
		if (uuid)
			ctx.walking.insert(QString::fromUtf8(uuid));
		obs_scene_enum_items(scene, enumSceneItems, &ctx);
	}
	obs_frontend_source_list_free(&list);
	return membership;
}

QJsonArray ObsAudio::listInputs() const
{
	return listSnapshot().value(QStringLiteral("inputs")).toArray();
}

QJsonObject ObsAudio::listSnapshot() const
{
	const QHash<QString, QStringList> membership = buildSceneMembership();
	QJsonArray inputs;
	struct Ctx {
		QJsonArray *list;
		const QHash<QString, QStringList> *membership;
	} ctx{&inputs, &membership};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *c = static_cast<Ctx *>(param);
			if (isAudioInput(source))
				c->list->append(describeSource(source, c->membership));
			return true;
		},
		&ctx);

	QJsonArray scenes;
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		const char *name = obs_source_get_name(list.sources.array[i]);
		if (name && name[0])
			scenes.append(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&list);

	QString current;
	obs_source_t *program = obs_frontend_get_current_scene();
	if (program) {
		const char *name = obs_source_get_name(program);
		if (name)
			current = QString::fromUtf8(name);
		obs_source_release(program);
	}

	QJsonObject payload;
	payload.insert("inputs", inputs);
	if (!current.isEmpty())
		payload.insert("currentProgramScene", current);
	if (!scenes.isEmpty())
		payload.insert("scenes", scenes);
	return payload;
}

void ObsAudio::invokeOnQt(const std::function<void()> &fn)
{
	QPointer<ObsAudio> self(this);
	QMetaObject::invokeMethod(
		this,
		[self, fn]() {
			if (self)
				fn();
		},
		Qt::QueuedConnection);
}

void ObsAudio::onSourceCreate(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->trackSource(ref);
		obs_source_release(ref);
		emit self->inputsChanged();
	});
}

void ObsAudio::onSourceDestroy(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !source)
		return;
	const char *uuid = obs_source_get_uuid(source);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	self->invokeOnQt([self, id]() {
		self->trackedIds.remove(id);
		emit self->inputsChanged();
	});
}

void ObsAudio::onSourceMute(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->emitState(ref);
		obs_source_release(ref);
	});
}

void ObsAudio::onSourceVolume(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->emitState(ref);
		obs_source_release(ref);
	});
}

void ObsAudio::onSourceRename(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsAudio *>(data);
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!self || !isAudioInput(source))
		return;
	obs_source_t *ref = obs_source_get_ref(source);
	self->invokeOnQt([self, ref]() {
		self->emitState(ref);
		emit self->inputsChanged();
		obs_source_release(ref);
	});
}

void ObsAudio::trackSource(obs_source_t *source)
{
	if (!isAudioInput(source))
		return;
	const char *uuid = obs_source_get_uuid(source);
	const QString id = QString::fromUtf8(uuid ? uuid : "");
	if (id.isEmpty() || trackedIds.contains(id))
		return;
	trackedIds.insert(id);
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_connect(sh, "mute", onSourceMute, this);
	signal_handler_connect(sh, "volume", onSourceVolume, this);
	signal_handler_connect(sh, "rename", onSourceRename, this);
}

void ObsAudio::untrackSource(obs_source_t *source)
{
	if (!source)
		return;
	signal_handler_t *sh = obs_source_get_signal_handler(source);
	signal_handler_disconnect(sh, "mute", onSourceMute, this);
	signal_handler_disconnect(sh, "volume", onSourceVolume, this);
	signal_handler_disconnect(sh, "rename", onSourceRename, this);
	const char *uuid = obs_source_get_uuid(source);
	trackedIds.remove(QString::fromUtf8(uuid ? uuid : ""));
}

void ObsAudio::emitState(obs_source_t *source)
{
	if (!isAudioInput(source))
		return;
	const QHash<QString, QStringList> membership = buildSceneMembership();
	emit inputStateChanged(describeSource(source, &membership));
}

void ObsAudio::onSceneItemMutated(void *data, calldata_t *)
{
	auto *self = static_cast<ObsAudio *>(data);
	if (!self)
		return;
	self->invokeOnQt([self]() { emit self->inputsChanged(); });
}

void ObsAudio::trackScenes()
{
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *src = list.sources.array[i];
		const char *uuid = obs_source_get_uuid(src);
		const QString id = QString::fromUtf8(uuid ? uuid : "");
		if (id.isEmpty() || trackedSceneIds.contains(id))
			continue;
		trackedSceneIds.insert(id);
		signal_handler_t *sh = obs_source_get_signal_handler(src);
		signal_handler_connect(sh, "item_add", onSceneItemMutated, this);
		signal_handler_connect(sh, "item_remove", onSceneItemMutated, this);
	}
	obs_frontend_source_list_free(&list);
}

void ObsAudio::untrackScenes()
{
	obs_frontend_source_list list{};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		signal_handler_t *sh = obs_source_get_signal_handler(list.sources.array[i]);
		signal_handler_disconnect(sh, "item_add", onSceneItemMutated, this);
		signal_handler_disconnect(sh, "item_remove", onSceneItemMutated, this);
	}
	obs_frontend_source_list_free(&list);
	trackedSceneIds.clear();
}

void ObsAudio::start()
{
	if (running)
		return;
	running = true;
	signal_handler_t *core = obs_get_signal_handler();
	signal_handler_connect(core, "source_create", onSourceCreate, this);
	signal_handler_connect(core, "source_destroy", onSourceDestroy, this);
	refresh();
	trackScenes();
}

void ObsAudio::stop()
{
	if (!running)
		return;
	running = false;
	signal_handler_t *core = obs_get_signal_handler();
	signal_handler_disconnect(core, "source_create", onSourceCreate, this);
	signal_handler_disconnect(core, "source_destroy", onSourceDestroy, this);
	untrackScenes();

	struct Ctx {
		ObsAudio *self;
	} ctx{this};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			static_cast<Ctx *>(param)->self->untrackSource(source);
			return true;
		},
		&ctx);
	trackedIds.clear();
}

void ObsAudio::refresh()
{
	struct Ctx {
		ObsAudio *self;
	} ctx{this};
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			static_cast<Ctx *>(param)->self->trackSource(source);
			return true;
		},
		&ctx);
	trackScenes();
}

obs_source_t *ObsAudio::findSource(const QString &name, const QString &id) const
{
	if (!id.isEmpty()) {
		obs_source_t *byId = obs_get_source_by_uuid(id.toUtf8().constData());
		if (byId)
			return byId;
	}
	if (name.isEmpty())
		return nullptr;
	return obs_get_source_by_name(name.toUtf8().constData());
}

bool ObsAudio::setMuted(const QString &name, bool muted, const QString &id)
{
	obs_source_t *source = findSource(name, id);
	if (!source)
		return false;
	if (!isAudioInput(source)) {
		obs_source_release(source);
		return false;
	}
	obs_source_set_muted(source, muted);
	obs_source_release(source);
	return true;
}

bool ObsAudio::setVolumeDb(const QString &name, double volumeDb, const QString &id)
{
	obs_source_t *source = findSource(name, id);
	if (!source)
		return false;
	if (!isAudioInput(source)) {
		obs_source_release(source);
		return false;
	}
	const double clamped = std::clamp(volumeDb, kMinVolumeDb, kMaxVolumeDb);
	obs_source_set_volume(source, dbToVolume(clamped));
	obs_source_release(source);
	return true;
}
