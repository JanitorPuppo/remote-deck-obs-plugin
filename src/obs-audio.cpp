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

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>

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

QJsonObject ObsAudio::describeSource(obs_source_t *source)
{
	QJsonObject obj;
	const char *uuid = obs_source_get_uuid(source);
	const char *name = obs_source_get_name(source);
	obj.insert("id", QString::fromUtf8(uuid ? uuid : ""));
	obj.insert("name", QString::fromUtf8(name ? name : ""));
	obj.insert("muted", obs_source_muted(source));
	obj.insert("volumeDb", volumeToDb(obs_source_get_volume(source)));
	return obj;
}

bool ObsAudio::enumInputsCallback(void *param, obs_source_t *source)
{
	auto *list = static_cast<QJsonArray *>(param);
	if (isAudioInput(source))
		list->append(describeSource(source));
	return true;
}

QJsonArray ObsAudio::listInputs() const
{
	QJsonArray list;
	obs_enum_sources(enumInputsCallback, &list);
	return list;
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
	emit inputStateChanged(describeSource(source));
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
}

void ObsAudio::stop()
{
	if (!running)
		return;
	running = false;
	signal_handler_t *core = obs_get_signal_handler();
	signal_handler_disconnect(core, "source_create", onSourceCreate, this);
	signal_handler_disconnect(core, "source_destroy", onSourceDestroy, this);

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
