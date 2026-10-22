/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/platform_device_id.h"

#include "base/bytes.h"

#include <QtCore/QUuid>

#include <windows.h>

namespace Platform {
namespace {

constexpr auto kSmbiosSystemInformation = BYTE(1);
constexpr auto kSmbiosUuidOffset = 0x08;
constexpr auto kSmbiosUuidSize = 16;
constexpr auto kSmbiosHeaderSize = 4;

const auto kCryptographyKey = L"SOFTWARE\\Microsoft\\Cryptography";
const auto kStoredKey = L"Software\\DarkMoon";
const auto kStoredValue = L"DeviceId";

struct RawSmbiosData {
	BYTE used20CallingMethod;
	BYTE majorVersion;
	BYTE minorVersion;
	BYTE dmiRevision;
	DWORD length;
	BYTE tableData[1];
};

[[nodiscard]] QString ReadRegistryString(
		HKEY root,
		const wchar_t *path,
		const wchar_t *name,
		REGSAM access) {
	auto key = HKEY();
	if (RegOpenKeyEx(root, path, 0, KEY_READ | access, &key)
		!= ERROR_SUCCESS) {
		return QString();
	}
	auto type = DWORD();
	auto buffer = std::array<WCHAR, 256>();
	auto size = DWORD(buffer.size() * sizeof(WCHAR));
	const auto result = RegQueryValueEx(
		key,
		name,
		nullptr,
		&type,
		reinterpret_cast<BYTE*>(buffer.data()),
		&size);
	RegCloseKey(key);
	if (result != ERROR_SUCCESS || type != REG_SZ) {
		return QString();
	}
	const auto length = std::min(
		int(size / sizeof(WCHAR)),
		int(buffer.size()));
	return QString::fromWCharArray(buffer.data(), length)
		.remove(QChar(0))
		.trimmed()
		.toLower();
}

[[nodiscard]] QByteArray MachineGuidSource() {
	const auto guid = ReadRegistryString(
		HKEY_LOCAL_MACHINE,
		kCryptographyKey,
		L"MachineGuid",
		KEY_WOW64_64KEY);
	return guid.isEmpty() ? QByteArray() : ("win-guid:" + guid.toUtf8());
}

[[nodiscard]] bool IsPlaceholderUuid(bytes::const_span uuid) {
	constexpr auto kPlaceholderLittleEndian = std::array<uchar, 16>{
		0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05,
		0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x09,
	};
	constexpr auto kPlaceholderBigEndian = std::array<uchar, 16>{
		0x03, 0x00, 0x02, 0x00, 0x04, 0x00, 0x05, 0x00,
		0x00, 0x06, 0x00, 0x07, 0x00, 0x08, 0x00, 0x09,
	};
	const auto same = [&](const std::array<uchar, 16> &pattern) {
		return !memcmp(uuid.data(), pattern.data(), pattern.size());
	};
	const auto first = uuid[0];
	const auto allSame = ranges::all_of(uuid, [&](bytes::type value) {
		return value == first;
	});
	return allSame
		|| same(kPlaceholderLittleEndian)
		|| same(kPlaceholderBigEndian);
}

[[nodiscard]] QByteArray SmbiosUuidSource() {
	const auto signature = DWORD('RSMB');
	const auto size = GetSystemFirmwareTable(signature, 0, nullptr, 0);
	if (size <= offsetof(RawSmbiosData, tableData)) {
		return QByteArray();
	}
	auto buffer = QByteArray(int(size), Qt::Uninitialized);
	if (GetSystemFirmwareTable(signature, 0, buffer.data(), size) != size) {
		return QByteArray();
	}
	const auto raw = reinterpret_cast<const RawSmbiosData*>(
		buffer.constData());
	const auto tableOffset = int(offsetof(RawSmbiosData, tableData));
	const auto table = bytes::make_span(buffer).subspan(
		tableOffset,
		std::min(int(raw->length), int(size) - tableOffset));
	auto offset = 0;
	while (offset + kSmbiosHeaderSize <= int(table.size())) {
		const auto type = BYTE(table[offset]);
		const auto length = int(BYTE(table[offset + 1]));
		if (length < kSmbiosHeaderSize
			|| offset + length > int(table.size())) {
			break;
		}
		if (type == kSmbiosSystemInformation
			&& length >= kSmbiosUuidOffset + kSmbiosUuidSize) {
			const auto uuid = table.subspan(
				offset + kSmbiosUuidOffset,
				kSmbiosUuidSize);
			if (IsPlaceholderUuid(uuid)) {
				return QByteArray();
			}
			const auto value = QByteArray(
				reinterpret_cast<const char*>(uuid.data()),
				uuid.size());
			return "win-smbios:" + value.toHex();
		}
		auto strings = offset + length;
		while (strings + 1 < int(table.size())
			&& (table[strings] != bytes::type(0)
				|| table[strings + 1] != bytes::type(0))) {
			++strings;
		}
		offset = strings + 2;
	}
	return QByteArray();
}

[[nodiscard]] QByteArray StoredRandomSource() {
	auto stored = ReadRegistryString(
		HKEY_CURRENT_USER,
		kStoredKey,
		kStoredValue,
		0);
	if (stored.isEmpty()) {
		auto key = HKEY();
		if (RegCreateKeyEx(
				HKEY_CURRENT_USER,
				kStoredKey,
				0,
				nullptr,
				REG_OPTION_NON_VOLATILE,
				KEY_SET_VALUE,
				nullptr,
				&key,
				nullptr) != ERROR_SUCCESS) {
			return QByteArray();
		}
		const auto generated = QUuid::createUuid().toString(
			QUuid::WithoutBraces);
		const auto wide = generated.toStdWString();
		const auto result = RegSetValueEx(
			key,
			kStoredValue,
			0,
			REG_SZ,
			reinterpret_cast<const BYTE*>(wide.c_str()),
			DWORD((wide.size() + 1) * sizeof(WCHAR)));
		RegCloseKey(key);
		if (result != ERROR_SUCCESS) {
			return QByteArray();
		}
		stored = generated;
	}
	return "win-rand:" + stored.toUtf8();
}

} // namespace

QByteArray DeviceIdSource() {
	if (auto guid = MachineGuidSource(); !guid.isEmpty()) {
		return guid;
	} else if (auto smbios = SmbiosUuidSource(); !smbios.isEmpty()) {
		return smbios;
	}
	return StoredRandomSource();
}

} // namespace Platform
