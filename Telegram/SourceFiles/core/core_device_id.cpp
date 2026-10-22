/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_device_id.h"

#include "base/bytes.h"
#include "base/openssl_help.h"
#include "platform/platform_device_id.h"

namespace Core {
namespace {

constexpr auto kDeviceIdSalt = "DarkMoon device_id v1";
constexpr auto kDeviceIdLength = 32;

[[nodiscard]] QString ComputeDeviceId() {
	const auto source = Platform::DeviceIdSource();
	if (source.isEmpty()) {
		LOG(("Device Id Error: no source available."));
		return QString();
	}
	const auto salt = QByteArray(kDeviceIdSalt);
	const auto hash = openssl::HmacSha256(
		bytes::make_span(salt),
		bytes::make_span(source));
	const auto raw = QByteArray(
		reinterpret_cast<const char*>(hash.data()),
		hash.size());
	return QString::fromLatin1(raw.toHex().left(kDeviceIdLength));
}

} // namespace

QString DeviceId() {
	static const auto result = ComputeDeviceId();
	return result;
}

} // namespace Core
