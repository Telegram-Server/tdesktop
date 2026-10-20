/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_signup.h"

#include "boxes/abstract_box.h"
#include "intro/intro_widget.h"
#include "core/file_utilities.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "ui/controls/userpic_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "styles/style_intro.h"
#include "styles/style_userpic_button.h"

namespace Intro {
namespace details {

SignupWidget::SignupWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _photo(
	this,
	data->controller,
	Ui::UserpicButton::Role::ChoosePhoto,
	st::defaultUserpicButton)
, _first(this, st::introName, tr::lng_signup_firstname())
, _last(this, st::introName, tr::lng_signup_lastname())
, _invertOrder(langFirstNameGoesSecond()) {
	_photo->showCustomOnChosen();

	Lang::Updated(
	) | rpl::on_next([=] {
		refreshLang();
	}, lifetime());

	const auto submitted = [=] { submit(); };
	_first->submits(
	) | rpl::on_next(submitted, _first->lifetime());
	_last->submits(
	) | rpl::on_next(submitted, _last->lifetime());

	if (_invertOrder) {
		setTabOrder(_last, _first);
	} else {
		setTabOrder(_first, _last);
	}

	setErrorCentered(true);

	setTitleText(tr::lng_signup_title());
	setDescriptionText(tr::lng_signup_desc());
	setMouseTracking(true);
}

bool SignupWidget::hasBack() const {
	// After choosing a login password the user may go back to change it.
	return getData()->passwordSignup;
}

void SignupWidget::finishInit() {
	showTerms();
}

void SignupWidget::refreshLang() {
	_invertOrder = langFirstNameGoesSecond();
	if (_invertOrder) {
		setTabOrder(_last, _first);
	} else {
		setTabOrder(_first, _last);
	}
	updateControlsGeometry();
}

void SignupWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void SignupWidget::updateControlsGeometry() {
	auto photoRight = contentLeft() + st::introNextButton.width;
	auto photoTop = contentTop() + st::introPhotoTop;
	_photo->moveToLeft(photoRight - _photo->width(), photoTop);

	auto firstTop = contentTop() + st::introStepFieldTop;
	auto secondTop = firstTop + st::introName.heightMin + st::introPhoneTop;
	if (_invertOrder) {
		_last->moveToLeft(contentLeft(), firstTop);
		_first->moveToLeft(contentLeft(), secondTop);
	} else {
		_first->moveToLeft(contentLeft(), firstTop);
		_last->moveToLeft(contentLeft(), secondTop);
	}
}

void SignupWidget::setInnerFocus() {
	if (_invertOrder || _last->hasFocus()) {
		_last->setFocusFast();
	} else {
		_first->setFocusFast();
	}
}

void SignupWidget::activate() {
	Step::activate();
	_first->show();
	_last->show();
	_photo->show();
	setInnerFocus();
}

void SignupWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

void SignupWidget::nameSubmitDone(const MTPauth_Authorization &result) {
	finish(result);
}

void SignupWidget::nameSubmitFail(const MTP::Error &error) {
	_sentRequest = 0;
	if (MTP::IsFloodError(error)) {
		showError(tr::lng_flood_error());
		if (_invertOrder) {
			_first->setFocus();
		} else {
			_last->setFocus();
		}
		return;
	}

	const auto &err = error.type();
	if (getData()->passwordSignup) {
		if (err == u"SIGN_UP_DISABLED"_q) {
			showError(tr::lng_signup_closed());
			return;
		} else if (err == u"PHONE_NUMBER_OCCUPIED"_q) {
			showError(tr::lng_signup_password_occupied());
			return;
		} else if (err == u"PASSWORD_LOGIN_DISABLED"_q) {
			showError(tr::lng_phone_password_unavailable());
			return;
		}
	}
	if (err == u"PHONE_NUMBER_FLOOD"_q) {
		Ui::show(Ui::MakeInformBox(tr::lng_error_phone_flood()));
	} else if (err == u"PHONE_NUMBER_INVALID"_q
		|| err == u"PHONE_NUMBER_BANNED"_q
		|| err == u"PHONE_CODE_EXPIRED"_q
		|| err == u"PHONE_CODE_EMPTY"_q
		|| err == u"PHONE_CODE_INVALID"_q
		|| err == u"PHONE_NUMBER_OCCUPIED"_q) {
		goBack();
	} else if (err == "FIRSTNAME_INVALID") {
		showError(tr::lng_bad_name());
		_first->setFocus();
	} else if (err == "LASTNAME_INVALID") {
		showError(tr::lng_bad_name());
		_last->setFocus();
	} else if (!MTP::IgnoreError(error)) {
		showError(rpl::single(err));
		if (_invertOrder) {
			_last->setFocus();
		} else {
			_first->setFocus();
		}
	}
}

void SignupWidget::submit() {
	if (_sentRequest) {
		return;
	}
	if (_invertOrder) {
		if ((_last->hasFocus() || _last->getLastText().trimmed().length()) && !_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		} else if (!_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		}
	} else {
		if ((_first->hasFocus() || _first->getLastText().trimmed().length()) && !_last->getLastText().trimmed().length()) {
			_last->setFocus();
			return;
		} else if (!_first->getLastText().trimmed().length()) {
			_first->setFocus();
			return;
		}
	}

	const auto send = [&] {
		hideError();

		_firstName = _first->getLastText().trimmed();
		_lastName = _last->getLastText().trimmed();
		if (getData()->passwordSignup) {
			sendSignUpWithPassword();
		} else {
			sendSignUp();
		}
	};
	if (_termsAccepted
		|| getData()->termsLock.text.text.isEmpty()
		|| !getData()->termsLock.popup) {
		send();
	} else {
		acceptTerms(crl::guard(this, [=] {
			_termsAccepted = true;
			send();
		}));
	}
}

void SignupWidget::sendSignUp() {
	_sentRequest = api().request(MTPauth_SignUp(
		MTP_flags(0),
		MTP_string(getData()->phone),
		MTP_bytes(getData()->phoneHash),
		MTP_string(_firstName),
		MTP_string(_lastName)
	)).done([=](const MTPauth_Authorization &result) {
		nameSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		nameSubmitFail(error);
	}).handleFloodErrors().send();
}

void SignupWidget::sendSignUpWithPassword() {
	// No login code: the account is created with its login password.
	using Flag = MTPDaccount_passwordInputSettings::Flag;
	_sentRequest = api().request(MTPtelesrv_SignUpWithPassword(
		MTP_flags(0),
		MTP_string(getData()->passwordLoginPhone),
		MTP_string(_firstName),
		MTP_string(_lastName),
		MTP_account_passwordInputSettings(
			MTP_flags(Flag::f_new_algo | Flag::f_new_password_hash),
			Core::PrepareCloudPasswordAlgo(getData()->passwordSignupAlgo),
			MTP_bytes(getData()->passwordSignupHash),
			MTPstring(), // hint
			MTPstring(), // email
			MTPSecureSecretSettings())
	)).done([=](const MTPauth_Authorization &result) {
		nameSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		nameSubmitFail(error);
	}).handleFloodErrors().send();
}

rpl::producer<QString> SignupWidget::nextButtonText() const {
	return tr::lng_intro_finish();
}

} // namespace details
} // namespace Intro
