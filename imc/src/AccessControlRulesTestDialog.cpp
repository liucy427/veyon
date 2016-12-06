/*
 * AccessControlRulesTestDialog.cpp - dialog for testing access control rules
 *
 * Copyright (c) 2016 Tobias Doerffel <tobydox/at/users/dot/sf/dot/net>
 *
 * This file is part of iTALC - http://italc.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <QMessageBox>

#include "AccessControlRulesTestDialog.h"
#include "AccessControlProvider.h"

#include "ui_AccessControlRulesTestDialog.h"


AccessControlRulesTestDialog::AccessControlRulesTestDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::AccessControlRulesTestDialog)
{
	ui->setupUi(this);
}



AccessControlRulesTestDialog::~AccessControlRulesTestDialog()
{
	delete ui;
}



void AccessControlRulesTestDialog::accept()
{
	AccessControlRule::Action result =
			AccessControlProvider().processAccessControlRules( ui->accessingUserLineEdit->text(),
															   ui->accessingComputerLineEdit->text(),
															   ui->localUserLineEdit->text(),
															   ui->localComputerLineEdit->text() );
	QString resultText;

	switch( result )
	{
	case AccessControlRule::ActionAllow:
		resultText = tr( "The access in the given scenario is allowed." );
		break;
	case AccessControlRule::ActionDeny:
		resultText = tr( "The access in the given scenario is denied." );
		break;
	}

	QMessageBox::information( this, tr( "Test result"),
							  resultText );
}
