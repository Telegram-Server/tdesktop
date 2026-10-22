/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_device_id.h"

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

namespace Platform {

QByteArray DeviceIdSource() {
	const auto service = IOServiceGetMatchingService(
		MACH_PORT_NULL,
		IOServiceMatching("IOPlatformExpertDevice"));
	if (!service) {
		return QByteArray();
	}
	const auto property = IORegistryEntryCreateCFProperty(
		service,
		CFSTR(kIOPlatformUUIDKey),
		kCFAllocatorDefault,
		0);
	IOObjectRelease(service);
	if (!property) {
		return QByteArray();
	}
	auto result = QByteArray();
	if (CFGetTypeID(property) == CFStringGetTypeID()) {
		const auto uuid = QString::fromCFString(CFStringRef(property))
			.trimmed()
			.toLower();
		if (!uuid.isEmpty()) {
			result = "mac-uuid:" + uuid.toUtf8();
		}
	}
	CFRelease(property);
	return result;
}

} // namespace Platform
