/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"
#include "core/core_cloud_password.h"

namespace Ui {
class PasswordInput;
class LinkButton;
} // namespace Ui

namespace Intro {
namespace details {

// Sign in with the telesrv login password, see telesrv.tl.
class LoginPasswordWidget final : public Step {
public:
	LoginPasswordWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void setInnerFocus() override;
	void activate() override;
	void cancelled() override;
	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

	bool hasBack() const override {
		return true;
	}

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	int errorTop() const override;

	void refreshLang();
	void updateControlsGeometry();

	void requestChallenge();
	void challengeDone(const MTPaccount_Password &result);
	void challengeFail(const MTP::Error &error);
	void checkPassword();
	void signInDone(const MTPauth_Authorization &result);
	void signInFail(const MTP::Error &error);
	void gotCloudPassword(const MTPaccount_Password &result);
	void retryWithNewChallenge();
	void passwordNotSet();
	void passwordLoginDisabled();
	void toCode();
	void showPasswordError(rpl::producer<QString> text);
	void serverError();

	Core::CloudPasswordCheckRequest _request;
	QByteArray _password;
	crl::time _lastRetryTime = 0;

	object_ptr<Ui::PasswordInput> _pwdField;
	object_ptr<Ui::LinkButton> _toCode;
	mtpRequestId _sentRequest = 0;

};

} // namespace details
} // namespace Intro
