/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_device_id.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>

namespace Platform {
namespace {

constexpr auto kMaxIdSize = 128;

[[nodiscard]] QByteArray ReadId(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.read(kMaxIdSize).trimmed().toLower();
}

[[nodiscard]] QByteArray MachineIdSource() {
	for (const auto &path : {
		u"/etc/machine-id"_q,
		u"/var/lib/dbus/machine-id"_q,
	}) {
		const auto id = ReadId(path);
		if (!id.isEmpty() && id != "uninitialized") {
			return "linux-mid:" + id;
		}
	}
	return QByteArray();
}

[[nodiscard]] QByteArray StoredRandomSource() {
	const auto folder = QStandardPaths::writableLocation(
		QStandardPaths::GenericDataLocation) + u"/DarkMoon"_q;
	const auto path = folder + u"/device-id"_q;
	auto stored = ReadId(path);
	if (stored.isEmpty()) {
		const auto generated = QUuid::createUuid().toString(
			QUuid::WithoutBraces).toUtf8();
		auto file = QSaveFile(path);
		if (!QDir().mkpath(folder)
			|| !file.open(QIODevice::WriteOnly)
			|| file.write(generated) != generated.size()
			|| !file.commit()) {
			return QByteArray();
		}
		stored = generated;
	}
	return "linux-rand:" + stored;
}

} // namespace

QByteArray DeviceIdSource() {
	if (auto id = MachineIdSource(); !id.isEmpty()) {
		return id;
	}
	return StoredRandomSource();
}

} // namespace Platform
