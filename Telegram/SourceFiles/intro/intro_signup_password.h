/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "intro/intro_step.h"

namespace Ui {
class PasswordInput;
} // namespace Ui

namespace Intro {
namespace details {

// Choose the telesrv login password of a new account, see telesrv.tl.
class SignupPasswordWidget final : public Step {
public:
	SignupPasswordWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void setInnerFocus() override;
	void activate() override;
	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

	bool hasBack() const override {
		return true;
	}

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateControlsGeometry();

	object_ptr<Ui::PasswordInput> _password;
	object_ptr<Ui::PasswordInput> _repeat;

};

} // namespace details
} // namespace Intro
