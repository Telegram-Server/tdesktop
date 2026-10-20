/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_phone.h"

#include "lang/lang_keys.h"
#include "intro/intro_code.h"
#include "intro/intro_email.h"
#include "intro/intro_login_password.h"
#include "intro/intro_qr.h"
#include "intro/intro_signup_password.h"
#include "intro/intro_widget.h"
#include "base/random.h"
#include "core/update_checker.h"
#include "styles/style_intro.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/fields/special_fields.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "data/data_user.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/abstract_box.h"
#include "boxes/phone_banned_box.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "countries/countries_instance.h" // Countries::Groups

namespace Intro {
namespace details {
namespace {

[[nodiscard]] bool AllowPhoneAttempt(const QString &phone) {
	const auto digits = ranges::count_if(
		phone,
		[](QChar ch) { return ch.isNumber(); });
	return (digits > 1);
}

[[nodiscard]] QString DigitsOnly(QString value) {
	static const auto RegExp = QRegularExpression("[^0-9]");
	return value.replace(RegExp, QString());
}

} // namespace

PhoneWidget::PhoneWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _country(
	this,
	getData()->controller->uiShow(),
	st::introCountry)
, _code(this, st::introCountryCode)
, _phone(
	this,
	st::introPhone,
	[](const QString &s) { return Countries::Groups(s); })
, _checkRequestTimer([=] { checkRequest(); }) {
	_code->setAccessibleName(tr::lng_country_code(tr::now));
	_phone->setAccessibleName(tr::lng_phone_number(tr::now));
	_phone->frontBackspaceEvent(
	) | rpl::on_next([=](not_null<QKeyEvent*> e) {
		_code->startErasing(e);
	}, _code->lifetime());

	_country->codeChanged(
	) | rpl::on_next([=](const QString &code) {
		_code->codeSelected(code);
		_phone->chooseCode(code);
	}, _country->lifetime());
	_code->codeChanged(
	) | rpl::on_next([=](const QString &code) {
		_country->onChooseCode(code);
		_phone->chooseCode(code);
	}, _code->lifetime());
	_code->addedToNumber(
	) | rpl::on_next([=](const QString &added) {
		_phone->addedToNumber(added);
	}, _phone->lifetime());
	_code->spacePressed() | rpl::on_next([=] {
		submit();
	}, _code->lifetime());
	connect(_phone, &Ui::PhonePartInput::changed, [=] { phoneChanged(); });
	connect(_code, &Ui::CountryCodeInput::changed, [=] { phoneChanged(); });

	setTitleText(tr::lng_phone_title());
	setDescriptionText(tr::lng_phone_desc());
	getData()->updated.events(
	) | rpl::on_next([=] {
		countryChanged();
	}, lifetime());
	setErrorCentered(true);
	setupQrLogin();
	setupPasswordLogin();

	if (!_country->chooseCountry(getData()->country)) {
		_country->chooseCountry(u"US"_q);
	}
	_changed = false;
}

QString PhoneWidget::accessibilityName() {
	return tr::lng_phone_title(tr::now);
}

void PhoneWidget::setupQrLogin() {
	const auto qrLogin = Ui::CreateChild<Ui::LinkButton>(
		this,
		tr::lng_phone_to_qr(tr::now));
	qrLogin->show();

	DEBUG_LOG(("PhoneWidget.qrLogin link created and shown."));

	rpl::combine(
		sizeValue(),
		qrLogin->widthValue()
	) | rpl::on_next([=](QSize size, int qrLoginWidth) {
		qrLogin->moveToLeft(
			(size.width() - qrLoginWidth) / 2,
			contentTop() + st::introQrLoginLinkTop);
	}, qrLogin->lifetime());

	qrLogin->setClickedCallback([=] {
		goNextOrBack<QrWidget>();
	});
}

void PhoneWidget::setupPasswordLogin() {
	_passwordLogin = Ui::CreateChild<Ui::LinkButton>(
		this,
		tr::lng_phone_to_password(tr::now));

	rpl::combine(
		sizeValue(),
		_passwordLogin->widthValue()
	) | rpl::on_next([=](QSize size, int linkWidth) {
		_passwordLogin->moveToLeft(
			(size.width() - linkWidth) / 2,
			contentTop() + st::introPasswordLoginLinkTop);
	}, _passwordLogin->lifetime());

	_passwordLogin->setClickedCallback([=] {
		getData()->passwordLogin = !getData()->passwordLogin;
		refreshPasswordLogin();
		hidePhoneError();
		_phone->setFocus();
	});

	Lang::Updated(
	) | rpl::on_next([=] {
		refreshPasswordLogin();
	}, _passwordLogin->lifetime());

	account().appConfig().value(
	) | rpl::on_next([=] {
		refreshPasswordLogin();
	}, _passwordLogin->lifetime());
}

void PhoneWidget::refreshPasswordLogin() {
	const auto available = account().appConfig().passwordLoginAvailable();
	if (!available) {
		getData()->passwordLogin = false;
	}
	const auto password = getData()->passwordLogin;
	_passwordLogin->setText(password
		? tr::lng_phone_to_code(tr::now)
		: tr::lng_phone_to_password(tr::now));
	setDescriptionText(password
		? tr::lng_phone_password_desc()
		: tr::lng_phone_desc());
	if (!available) {
		_passwordLogin->hide();
	} else if (!animating()) {
		_passwordLogin->show();
	}
}

void PhoneWidget::switchToCodeLogin(bool submitNow) {
	getData()->passwordLogin = false;
	refreshPasswordLogin();
	if (submitNow) {
		submit();
	}
}

void PhoneWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	_country->moveToLeft(contentLeft(), contentTop() + st::introStepFieldTop);
	auto phoneTop = _country->y() + _country->height() + st::introPhoneTop;
	_code->moveToLeft(contentLeft(), phoneTop);
	_phone->moveToLeft(contentLeft() + _country->width() - st::introPhone.width, phoneTop);
}

void PhoneWidget::showPhoneError(rpl::producer<QString> text) {
	_phone->showError();
	showError(std::move(text));
}

void PhoneWidget::hidePhoneError() {
	hideError();
}

void PhoneWidget::countryChanged() {
	if (!_changed) {
		selectCountry(getData()->country);
	}
}

void PhoneWidget::phoneChanged() {
	_changed = true;
	hidePhoneError();
}

void PhoneWidget::submit() {
	if (_sentRequest || isHidden()) {
		return;
	}

	{
		const auto hasCodeButWaitingPhone = _code->hasFocus()
			&& (_code->getLastText().size() > 1)
			&& _phone->getLastText().isEmpty();
		if (hasCodeButWaitingPhone) {
			_phone->hideError();
			_phone->setFocus();
			return;
		}
	}
	const auto phone = fullNumber();
	if (!AllowPhoneAttempt(phone)) {
		showPhoneError(tr::lng_bad_phone());
		_phone->setFocus();
		return;
	}

	cancelNearestDcRequest();

	// Check if such account is authorized already.
	const auto phoneDigits = DigitsOnly(phone);
	for (const auto &[index, existing] : Core::App().domain().accounts()) {
		const auto raw = existing.get();
		if (const auto session = raw->maybeSession()) {
			if (raw->mtp().environment() == account().mtp().environment()
				&& DigitsOnly(session->user()->phone()) == phoneDigits) {
				crl::on_main(raw, [=] {
					Core::App().domain().activate(raw);
				});
				return;
			}
		}
	}

	hidePhoneError();

	_checkRequestTimer.callEach(1000);

	_sentPhone = phone;
	api().instance().setUserPhone(_sentPhone);
	if (getData()->passwordLogin) {
		passwordSubmit();
		return;
	}
	_sentRequest = api().request(MTPauth_SendCode(
		MTP_string(_sentPhone),
		MTP_int(ApiId),
		MTP_string(ApiHash),
		MTP_codeSettings(
			MTP_flags(0),
			MTPVector<MTPbytes>(),
			MTPstring(),
			MTPBool())
	)).done([=](const MTPauth_SentCode &result) {
		phoneSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		phoneSubmitFail(error);
	}).handleFloodErrors().send();
}

void PhoneWidget::stopCheck() {
	_checkRequestTimer.cancel();
}

void PhoneWidget::checkRequest() {
	auto status = api().instance().state(_sentRequest);
	if (status < 0) {
		auto leftms = -status;
		if (leftms >= 1000) {
			api().request(base::take(_sentRequest)).cancel();
		}
	}
	if (!_sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void PhoneWidget::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	_sentRequest = 0;

	getData()->passwordSignup = false;
	result.match([&](const MTPDauth_sentCode &data) {
		fillSentCodeData(data);
		getData()->phone = DigitsOnly(_sentPhone);
		getData()->phoneHash = qba(data.vphone_code_hash());
		if (getData()->emailStatus == EmailStatus::SetupRequired) {
			return goNext<EmailWidget>();
		}
		const auto next = data.vnext_type();
		if (next && next->type() == mtpc_auth_codeTypeCall) {
			getData()->callStatus = CallStatus::Waiting;
			getData()->callTimeout = data.vtimeout().value_or(60);
		} else {
			getData()->callStatus = CallStatus::Disabled;
			getData()->callTimeout = 0;
		}
		goNext<CodeWidget>();
	}, [&](const MTPDauth_sentCodeSuccess &data) {
		finish(data.vauthorization());
	}, [](const MTPDauth_sentCodePaymentRequired &) {
		LOG(("API Error: Unexpected auth.sentCodePaymentRequired "
			"(PhoneWidget::phoneSubmitDone)."));
	});
}

void PhoneWidget::phoneSubmitFail(const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		stopCheck();
		_sentRequest = 0;
		showPhoneError(tr::lng_flood_error());
		return;
	}

	stopCheck();
	_sentRequest = 0;
	const auto &err = error.type();
	if (err == u"PHONE_NUMBER_FLOOD"_q) {
		Ui::show(Ui::MakeInformBox(tr::lng_error_phone_flood()));
	} else if (err == u"PHONE_NUMBER_INVALID"_q) { // show error
		showPhoneError(tr::lng_bad_phone());
	} else if (err == u"PHONE_NUMBER_BANNED"_q) {
		Ui::ShowPhoneBannedError(getData()->controller, _sentPhone);
	} else if (!MTP::IgnoreError(error)) {
		showPhoneError(rpl::single(err));
	}
}

void PhoneWidget::passwordSubmit() {
	_sentRequest = api().request(MTPtelesrv_GetLoginPassword(
		MTP_flags(0),
		MTP_string(_sentPhone)
	)).done([=](const MTPaccount_Password &result) {
		passwordSubmitDone(result);
	}).fail([=](const MTP::Error &error) {
		passwordSubmitFail(error);
	}).handleFloodErrors().send();
}

void PhoneWidget::passwordSubmitDone(const MTPaccount_Password &result) {
	stopCheck();
	_sentRequest = 0;

	const auto &data = result.data();
	base::RandomAddSeed(bytes::make_span(data.vsecure_random().v));
	auto state = Core::ParseCloudPasswordState(data);
	const auto supported = state.hasPassword
		? bool(state.mtp.request)
		: !v::is_null(state.mtp.newPassword);
	if (!supported) {
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

	getData()->phone = DigitsOnly(_sentPhone);
	getData()->phoneHash = QByteArray();
	getData()->termsLock = Window::TermsLock();
	getData()->passwordLoginPhone = _sentPhone;
	getData()->passwordSignup = false;
	const auto hasPassword = state.hasPassword;
	getData()->passwordLoginState = std::move(state);
	if (hasPassword) {
		goNext<LoginPasswordWidget>();
	} else {
		goNext<SignupPasswordWidget>();
	}
}

void PhoneWidget::passwordSubmitFail(const MTP::Error &error) {
	stopCheck();
	_sentRequest = 0;
	if (MTP::IsFloodError(error)) {
		showPhoneError(tr::lng_flood_error());
		return;
	}

	const auto &err = error.type();
	if (err == u"PASSWORD_LOGIN_NOT_SET"_q) {
		const auto confirmed = crl::guard(this, [=](Fn<void()> &&close) {
			close();
			switchToCodeLogin(true);
		});
		Ui::show(Ui::MakeConfirmBox({
			.text = tr::lng_phone_password_not_set(),
			.confirmed = confirmed,
			.confirmText = tr::lng_phone_to_code(),
		}));
	} else if (err == u"SIGN_UP_DISABLED"_q) {
		showPhoneError(tr::lng_signup_closed());
	} else if (err == u"PASSWORD_LOGIN_DISABLED"_q) {
		account().appConfig().refresh();
		switchToCodeLogin(false);
		showPhoneError(tr::lng_phone_password_unavailable());
	} else if (err == u"PHONE_NUMBER_INVALID"_q) {
		showPhoneError(tr::lng_bad_phone());
	} else if (err == u"PHONE_NUMBER_BANNED"_q) {
		Ui::ShowPhoneBannedError(getData()->controller, _sentPhone);
	} else if (!MTP::IgnoreError(error)) {
		showPhoneError(rpl::single(err));
	}
}

QString PhoneWidget::fullNumber() const {
	return _code->getLastText() + _phone->getLastText();
}

void PhoneWidget::selectCountry(const QString &country) {
	_country->chooseCountry(country);
}

void PhoneWidget::setInnerFocus() {
	_phone->setFocusFast();
}

void PhoneWidget::activate() {
	Step::activate();
	showChildren();
	refreshPasswordLogin();
	setInnerFocus();
	if (base::take(getData()->passwordLoginCodeRequested)) {
		switchToCodeLogin(true);
	}
}

void PhoneWidget::finished() {
	Step::finished();
	_checkRequestTimer.cancel();
	apiClear();

	cancelled();
}

void PhoneWidget::cancelled() {
	api().request(base::take(_sentRequest)).cancel();
}

} // namespace details
} // namespace Intro
