/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Platform {

// An id of this machine that outlives reinstalls and wiped tdata, prefixed
// by its kind so ids of different kinds never collide. Empty if none.
[[nodiscard]] QByteArray DeviceIdSource();

} // namespace Platform
