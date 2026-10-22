/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Data {

// telesrv extension: staff permissions from the telesrv_permissions key.
enum class TelesrvAction : uchar {
	Send,
	Delete,
	Edit,
	ClearHistory,
	CreateGroup,
	CreateChannel,
	JoinGroup,
	JoinChannel,
	LeaveGroup,
	LeaveChannel,
	AddContact,
};

class TelesrvPermissions final {
public:
	explicit TelesrvPermissions(not_null<Main::Session*> session);
	~TelesrvPermissions();

	[[nodiscard]] bool allows(TelesrvAction action) const;
	[[nodiscard]] rpl::producer<bool> allowsValue(TelesrvAction action) const;
	[[nodiscard]] rpl::producer<> value() const;
	[[nodiscard]] rpl::producer<> changes() const;

	[[nodiscard]] bool canSendTo(not_null<const PeerData*> peer) const;
	[[nodiscard]] QString sendError(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool inSendWhitelist(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool canClearHistory(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool canJoin(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool canJoinByType(bool broadcast) const;
	[[nodiscard]] bool canLeave(not_null<const PeerData*> peer) const;
	[[nodiscard]] bool canCreate(bool broadcast) const;

	[[nodiscard]] static QString DeniedText();

private:
	struct State {
		uint32 forbidden = 0;
		bool whitelistEnabled = false;
		std::vector<UserId> whitelist;

		friend inline bool operator==(const State &, const State &) = default;
	};

	void applyFromConfig();
	void apply(State &&state);
	[[nodiscard]] static State Parse(const MTPJSONValue &value);
	[[nodiscard]] static QString Serialize(const State &state);
	[[nodiscard]] static State Deserialize(const QString &data);

	const not_null<Main::Session*> _session;
	State _state;
	rpl::event_stream<> _changes;

	rpl::lifetime _lifetime;

};

} // namespace Data
