/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_signup_password.h"

#include "intro/intro_widget.h"
#include "intro/intro_signup.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "ui/widgets/fields/password_input.h"
#include "styles/style_intro.h"

namespace Intro {
namespace details {

SignupPasswordWidget::SignupPasswordWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _password(this, st::introPassword, tr::lng_signup_password_field())
, _repeat(this, st::introPassword, tr::lng_signup_password_repeat()) {
	connect(_password, &Ui::PasswordInput::changed, [=] { hideError(); });
	connect(_repeat, &Ui::PasswordInput::changed, [=] { hideError(); });
	setTabOrder(_password, _repeat);

	setErrorCentered(true);

	setTitleText(tr::lng_signup_password_title());
	setDescriptionText(tr::lng_signup_password_desc());
	setMouseTracking(true);
}

void SignupPasswordWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	updateControlsGeometry();
}

void SignupPasswordWidget::updateControlsGeometry() {
	const auto passwordTop = contentTop() + st::introStepFieldTop;
	const auto repeatTop = passwordTop
		+ st::introPassword.heightMin
		+ st::introPhoneTop;
	_password->moveToLeft(contentLeft(), passwordTop);
	_repeat->moveToLeft(contentLeft(), repeatTop);
}

void SignupPasswordWidget::setInnerFocus() {
	if (_repeat->hasFocus()) {
		_repeat->setFocusFast();
	} else {
		_password->setFocusFast();
	}
}

void SignupPasswordWidget::activate() {
	Step::activate();
	_password->show();
	_repeat->show();
	setInnerFocus();
}

void SignupPasswordWidget::submit() {
	const auto password = _password->getLastText();
	const auto repeat = _repeat->getLastText();
	if (password.isEmpty()) {
		_password->showError();
		_password->setFocus();
		return;
	} else if (_password->hasFocus() && repeat.isEmpty()) {
		_repeat->setFocus();
		return;
	} else if (repeat != password) {
		showError(tr::lng_cloud_password_differ());
		_repeat->selectAll();
		_repeat->showError();
		_repeat->setFocus();
		return;
	}
	hideError();

	const auto &algo = getData()->passwordLoginState.mtp.newPassword;
	const auto utf8 = password.toUtf8();
	auto digest = Core::ComputeCloudPasswordDigest(
		algo,
		bytes::make_span(utf8));
	if (digest.modpow.empty()) {
		showError(rpl::single(Lang::Hard::ServerError()));
		return;
	}
	getData()->passwordSignup = true;
	getData()->passwordSignupAlgo = algo;
	getData()->passwordSignupHash = std::move(digest.modpow);
	goNext<SignupWidget>();
}

rpl::producer<QString> SignupPasswordWidget::nextButtonText() const {
	return tr::lng_intro_next();
}

} // namespace details
} // namespace Intro
