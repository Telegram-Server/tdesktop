/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/components/telesrv_permissions.h"

#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "storage/storage_account.h"

namespace Data {
namespace {

constexpr auto kCachePref = "telesrv_permissions"_cs;
constexpr auto kCacheVersion = 1;

[[nodiscard]] uint32 Bit(TelesrvAction action) {
	return (uint32(1) << uint32(action));
}

[[nodiscard]] std::optional<TelesrvAction> ActionByKey(const QString &key) {
	using Action = TelesrvAction;
	static const auto map = base::flat_map<QString, Action>{
		{ u"send"_q, Action::Send },
		{ u"delete"_q, Action::Delete },
		{ u"edit"_q, Action::Edit },
		{ u"clear_history"_q, Action::ClearHistory },
		{ u"create_group"_q, Action::CreateGroup },
		{ u"create_channel"_q, Action::CreateChannel },
		{ u"join_group"_q, Action::JoinGroup },
		{ u"join_channel"_q, Action::JoinChannel },
		{ u"leave_group"_q, Action::LeaveGroup },
		{ u"leave_channel"_q, Action::LeaveChannel },
		{ u"add_contact"_q, Action::AddContact },
	};
	const auto i = map.find(key);
	return (i != end(map)) ? std::make_optional(i->second) : std::nullopt;
}

} // namespace

TelesrvPermissions::TelesrvPermissions(not_null<Main::Session*> session)
: _session(session)
, _state(Deserialize(session->local().readPref<QString>(kCachePref))) {
	_session->appConfig().value(
	) | rpl::filter([=] {
		return _session->appConfig().received();
	}) | rpl::on_next([=] {
		applyFromConfig();
	}, _lifetime);
}

TelesrvPermissions::~TelesrvPermissions() = default;

bool TelesrvPermissions::allows(TelesrvAction action) const {
	return !(_state.forbidden & Bit(action));
}

rpl::producer<bool> TelesrvPermissions::allowsValue(
		TelesrvAction action) const {
	return value() | rpl::map([=] {
		return allows(action);
	}) | rpl::distinct_until_changed();
}

rpl::producer<> TelesrvPermissions::value() const {
	return _changes.events_starting_with({});
}

rpl::producer<> TelesrvPermissions::changes() const {
	return _changes.events();
}

bool TelesrvPermissions::canSendTo(not_null<const PeerData*> peer) const {
	return sendError(peer).isEmpty();
}

QString TelesrvPermissions::sendError(
		not_null<const PeerData*> peer) const {
	if (peer->isSelf()) {
		return QString();
	} else if (!allows(TelesrvAction::Send)) {
		return tr::lng_telesrv_send_restricted(tr::now);
	} else if (!inSendWhitelist(peer)) {
		return tr::lng_telesrv_send_whitelist_restricted(tr::now);
	}
	return QString();
}

bool TelesrvPermissions::inSendWhitelist(
		not_null<const PeerData*> peer) const {
	return !peer->isUser()
		|| peer->isSelf()
		|| !_state.whitelistEnabled
		|| ranges::contains(_state.whitelist, peerToUser(peer->id));
}

bool TelesrvPermissions::canClearHistory(
		not_null<const PeerData*> peer) const {
	return !peer->isUser() || allows(TelesrvAction::ClearHistory);
}

bool TelesrvPermissions::canJoin(not_null<const PeerData*> peer) const {
	return canJoinByType(peer->isBroadcast());
}

bool TelesrvPermissions::canJoinByType(bool broadcast) const {
	return allows(broadcast
		? TelesrvAction::JoinChannel
		: TelesrvAction::JoinGroup);
}

bool TelesrvPermissions::canLeave(not_null<const PeerData*> peer) const {
	return peer->isUser()
		|| allows(peer->isBroadcast()
			? TelesrvAction::LeaveChannel
			: TelesrvAction::LeaveGroup);
}

bool TelesrvPermissions::canCreate(bool broadcast) const {
	return allows(broadcast
		? TelesrvAction::CreateChannel
		: TelesrvAction::CreateGroup);
}

QString TelesrvPermissions::DeniedText() {
	return tr::lng_telesrv_permission_denied(tr::now);
}

void TelesrvPermissions::applyFromConfig() {
	auto state = Parse(
		_session->appConfig().getJson(u"telesrv_permissions"_q));
	if (state == State()) {
		_session->local().clearPref(kCachePref);
	} else {
		_session->local().writePref<QString>(kCachePref, Serialize(state));
	}
	apply(std::move(state));
}

void TelesrvPermissions::apply(State &&state) {
	if (_state == state) {
		return;
	}
	_state = std::move(state);
	_changes.fire({});
}

TelesrvPermissions::State TelesrvPermissions::Parse(
		const MTPJSONValue &value) {
	auto result = State();
	if (value.type() != mtpc_jsonObject) {
		return result;
	}
	for (const auto &entry : value.c_jsonObject().vvalue().v) {
		const auto &data = entry.data();
		const auto key = qs(data.vkey());
		const auto &inner = data.vvalue();
		if (key == u"send_whitelist"_q) {
			if (inner.type() != mtpc_jsonObject) {
				continue;
			}
			for (const auto &field : inner.c_jsonObject().vvalue().v) {
				const auto &fieldData = field.data();
				const auto name = qs(fieldData.vkey());
				const auto &fieldValue = fieldData.vvalue();
				if (name == u"enabled"_q
					&& fieldValue.type() == mtpc_jsonBool) {
					result.whitelistEnabled = mtpIsTrue(
						fieldValue.c_jsonBool().vvalue());
				} else if (name == u"user_ids"_q
					&& fieldValue.type() == mtpc_jsonArray) {
					for (const auto &id : fieldValue.c_jsonArray().vvalue().v) {
						if (id.type() == mtpc_jsonNumber) {
							const auto bare = id.c_jsonNumber().vvalue().v;
							result.whitelist.push_back(
								UserId(BareId(base::SafeRound(bare))));
						}
					}
				}
			}
		} else if (inner.type() == mtpc_jsonBool
			&& !mtpIsTrue(inner.c_jsonBool().vvalue())) {
			if (const auto action = ActionByKey(key)) {
				result.forbidden |= Bit(*action);
			}
		}
	}
	ranges::sort(result.whitelist);
	return result;
}

QString TelesrvPermissions::Serialize(const State &state) {
	auto ids = QStringList();
	ids.reserve(state.whitelist.size());
	for (const auto &id : state.whitelist) {
		ids.push_back(QString::number(id.bare));
	}
	return QStringList{
		QString::number(kCacheVersion),
		QString::number(state.forbidden),
		state.whitelistEnabled ? u"1"_q : u"0"_q,
		ids.join(','),
	}.join(';');
}

TelesrvPermissions::State TelesrvPermissions::Deserialize(
		const QString &data) {
	const auto parts = data.split(';');
	if (parts.size() != 4
		|| parts[0].toInt() != kCacheVersion) {
		return State();
	}
	auto result = State{
		.forbidden = parts[1].toUInt(),
		.whitelistEnabled = (parts[2] == u"1"_q),
	};
	for (const auto &id : parts[3].split(',', Qt::SkipEmptyParts)) {
		result.whitelist.push_back(UserId(BareId(id.toULongLong())));
	}
	ranges::sort(result.whitelist);
	return result;
}

} // namespace Data
