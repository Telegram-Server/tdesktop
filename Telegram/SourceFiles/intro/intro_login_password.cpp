/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_login_password.h"

#include "intro/intro_widget.h"
#include "intro/intro_password_check.h"
#include "core/update_checker.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "base/random.h"
#include "boxes/abstract_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/format_values.h" // Ui::FormatPhone
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/password_input.h"
#include "styles/style_intro.h"
#include "styles/style_widgets.h"

namespace Intro {
namespace details {

LoginPasswordWidget::LoginPasswordWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _request(getData()->passwordLoginState.mtp.request)
, _pwdField(this, st::introPassword, tr::lng_login_password_field())
, _toCode(this, tr::lng_login_password_to_code(tr::now)) {
	Lang::Updated(
	) | rpl::on_next([=] {
		refreshLang();
	}, lifetime());

	_toCode->addClickHandler([=] { toCode(); });
	connect(_pwdField, &Ui::PasswordInput::changed, [=] { hideError(); });

	setTitleText(rpl::single(Ui::FormatPhone(getData()->phone)));
	setDescriptionText(tr::lng_login_password_desc());

	setMouseTracking(true);
}

void LoginPasswordWidget::refreshLang() {
	_toCode->setText(tr::lng_login_password_to_code(tr::now));
	updateControlsGeometry();
}

int LoginPasswordWidget::errorTop() const {
	return contentTop() + st::introErrorBelowLinkTop;
}

void LoginPasswordWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void LoginPasswordWidget::updateControlsGeometry() {
	_pwdField->moveToLeft(contentLeft(), contentTop() + st::introPasswordTop);
	const auto linkTop = _pwdField->y()
		+ _pwdField->height()
		+ st::introLinkTop;
	_toCode->moveToLeft(contentLeft() + st::buttonRadius, linkTop);
}

void LoginPasswordWidget::setInnerFocus() {
	_pwdField->setFocusFast();
}

void LoginPasswordWidget::activate() {
	Step::activate();
	_pwdField->show();
	_toCode->show();
	setInnerFocus();
}

void LoginPasswordWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

void LoginPasswordWidget::submit() {
	if (_sentRequest) {
		return;
	}
	const auto password = _pwdField->getLastText();
	if (password.isEmpty()) {
		_pwdField->showError();
		_pwdField->setFocus();
		return;
	}
	hideError();
	_password = password.toUtf8();

	// A challenge answers one attempt only, a retry needs a fresh one.
	if (_request) {
		checkPassword();
	} else {
		requestChallenge();
	}
}

void LoginPasswordWidget::requestChallenge() {
	_sentRequest = api().request(MTPtelesrv_GetLoginPassword(
		MTP_flags(0),
		MTP_string(getData()->passwordLoginPhone)
	)).done([=](const MTPaccount_Password &result) {
		challengeDone(result);
	}).fail([=](const MTP::Error &error) {
		challengeFail(error);
	}).handleFloodErrors().send();
}

void LoginPasswordWidget::challengeDone(const MTPaccount_Password &result) {
	_sentRequest = 0;
	const auto &data = result.data();
	base::RandomAddSeed(bytes::make_span(data.vsecure_random().v));
	const auto state = Core::ParseCloudPasswordState(data);
	if (!state.hasPassword) {
		// The account was deleted meanwhile, start over from the phone.
		goBack();
		return;
	} else if (!state.mtp.request) {
		serverError();
		return;
	}
	_request = state.mtp.request;
	checkPassword();
}

void LoginPasswordWidget::challengeFail(const MTP::Error &error) {
	_sentRequest = 0;
	if (MTP::IsFloodError(error)) {
		showPasswordError(tr::lng_flood_error());
		return;
	}
	const auto &type = error.type();
	if (type == u"PASSWORD_LOGIN_NOT_SET"_q) {
		passwordNotSet();
	} else if (type == u"PASSWORD_LOGIN_DISABLED"_q) {
		passwordLoginDisabled();
	} else if (type == u"PHONE_NUMBER_INVALID"_q) {
		goBack();
	} else if (!MTP::IgnoreError(error)) {
		showPasswordError(rpl::single(type));
	}
}

void LoginPasswordWidget::checkPassword() {
	const auto hash = Core::ComputeCloudPasswordHash(
		_request.algo,
		bytes::make_span(_password));
	const auto check = Core::ComputeCloudPasswordCheck(_request, hash);

	// The server burns the challenge whatever the answer is.
	_request = Core::CloudPasswordCheckRequest();
	if (!check) {
		serverError();
		return;
	}
	_sentRequest = api().request(MTPtelesrv_SignInWithPassword(
		MTP_flags(0),
		MTP_string(getData()->passwordLoginPhone),
		check.result
	)).done([=](const MTPauth_Authorization &result) {
		signInDone(result);
	}).fail([=](const MTP::Error &error) {
		signInFail(error);
	}).handleFloodErrors().send();
}

void LoginPasswordWidget::signInDone(const MTPauth_Authorization &result) {
	_sentRequest = 0;
	_password = QByteArray();
	finish(result);
}

void LoginPasswordWidget::signInFail(const MTP::Error &error) {
	_sentRequest = 0;
	if (MTP::IsFloodError(error)) {
		showPasswordError(tr::lng_flood_error());
		return;
	}
	const auto &type = error.type();
	if (type == u"PASSWORD_HASH_INVALID"_q) {
		showPasswordError(tr::lng_signin_bad_password());
		_pwdField->selectAll();
	} else if (type == u"SRP_ID_INVALID"_q
		|| type == u"SRP_PASSWORD_CHANGED"_q
		|| type == u"AUTH_KEY_PERM_EMPTY"_q) {
		retryWithNewChallenge();
	} else if (type == u"SESSION_PASSWORD_NEEDED"_q) {
		_sentRequest = api().request(MTPaccount_GetPassword(
		)).done([=](const MTPaccount_Password &result) {
			gotCloudPassword(result);
		}).fail([=](const MTP::Error &error) {
			signInFail(error);
		}).handleFloodErrors().send();
	} else if (type == u"PASSWORD_LOGIN_NOT_SET"_q) {
		passwordNotSet();
	} else if (type == u"PASSWORD_LOGIN_DISABLED"_q) {
		passwordLoginDisabled();
	} else if (type == u"PHONE_NUMBER_INVALID"_q) {
		goBack();
	} else if (!handleDeviceBindingError(type)
		&& !MTP::IgnoreError(error)) {
		showPasswordError(rpl::single(type));
	}
}

void LoginPasswordWidget::retryWithNewChallenge() {
	const auto now = crl::now();
	if (_lastRetryTime > 0
		&& now - _lastRetryTime < Core::kHandleSrpIdInvalidTimeout) {
		serverError();
		return;
	}
	_lastRetryTime = now;
	requestChallenge();
}

void LoginPasswordWidget::gotCloudPassword(
		const MTPaccount_Password &result) {
	_sentRequest = 0;
	_password = QByteArray();
	const auto &data = result.data();
	getData()->pwdState = Core::ParseCloudPasswordState(data);
	if (!data.vcurrent_algo() || !data.vsrp_id() || !data.vsrp_B()) {
		LOG(("API Error: No current password received on login."));
		serverError();
		return;
	} else if (!getData()->pwdState.hasPassword) {
		const auto callback = [=](Fn<void()> &&close) {
			Core::UpdateApplication();
			close();
		};
		Ui::show(Ui::MakeConfirmBox({
			.text = tr::lng_passport_app_out_of_date(),
			.confirmed = callback,
			.confirmText = tr::lng_menu_update(),
		}));
		return;
	}
	goReplace<PasswordCheckWidget>(Animate::Forward);
}

void LoginPasswordWidget::passwordNotSet() {
	const auto confirmed = crl::guard(this, [=](Fn<void()> &&close) {
		close();
		toCode();
	});
	Ui::show(Ui::MakeConfirmBox({
		.text = tr::lng_phone_password_not_set(),
		.confirmed = confirmed,
		.confirmText = tr::lng_phone_to_code(),
	}));
}

void LoginPasswordWidget::passwordLoginDisabled() {
	getData()->passwordLogin = false;
	account().appConfig().refresh();
	Ui::show(Ui::MakeInformBox(tr::lng_phone_password_unavailable()));
	goBack();
}

void LoginPasswordWidget::toCode() {
	getData()->passwordLogin = false;
	getData()->passwordLoginCodeRequested = true;
	goBack();
}

void LoginPasswordWidget::showPasswordError(rpl::producer<QString> text) {
	showError(std::move(text));
	_pwdField->showError();
	_pwdField->setFocus();
}

void LoginPasswordWidget::serverError() {
	showError(rpl::single(Lang::Hard::ServerError()));
}

rpl::producer<QString> LoginPasswordWidget::nextButtonText() const {
	return tr::lng_intro_submit();
}

} // namespace details
} // namespace Intro
