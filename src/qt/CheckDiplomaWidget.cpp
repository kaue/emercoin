﻿//CheckDiplomaWidget.cpp by Emercoin developers
#include "CheckDiplomaWidget.h"
#include <QFormLayout>
#include <QPushButton>
#include <QUrl>
#include <QDate>
#include <QToolTip>
#include <QDesktopServices>

const QString CheckDiplomaWidget::s_checkUniversity = "https://trusted-diploma.com/?univ=%1";
const QString CheckDiplomaWidget::s_checkStudent = s_checkUniversity + "&name=%2&%3_year=%4";
CheckDiplomaWidget::CheckDiplomaWidget() {
	setWindowTitle(tr("Check diploma"));

	auto form = new QFormLayout(this);

	const int emcMaxNameLen = 500;
	_name = new QLineEdit;
	_name->setMaxLength(emcMaxNameLen);
	form->addRow(tr("First name and last name"), _name);

	_university = new QLineEdit;
	_university->setMaxLength(emcMaxNameLen);
	form->addRow(tr("University"), _university);

	struct OptionalYearSpinBox: public QSpinBox {
		public:
			OptionalYearSpinBox() {
				setMinimum(-10000);
				setMaximum(std::numeric_limits<qint32>::max());
				setValue(0);//could be QDate::currentDate().year(), but make it empty
			}
			virtual QString textFromValue(int value)const override {
				if(value==0)
					return {};
				return QSpinBox::textFromValue(value);
			}
	};

	_yearAdmission  = new OptionalYearSpinBox;
	form->addRow(tr("Admission year"), _yearAdmission);

	_yearGraduation  = new OptionalYearSpinBox;
	form->addRow(tr("... or graduation year"), _yearGraduation);

	auto search = new QPushButton(tr("Check..."));
	search->setShortcut(QKeySequence("Return"));
	connect(search, &QPushButton::clicked, this, &CheckDiplomaWidget::onSearch);
	form->addRow(QString(), search);
}
void CheckDiplomaWidget::onSearch() {
	auto showMsg = [](QWidget*w) {
		auto pt = w->rect().bottomLeft();
		w->setFocus();
		QToolTip::showText(w->mapToGlobal(pt), tr("This field can't be empty"));
	};
	QString name = _name->text().simplified().trimmed();
	QString univ = _university->text().simplified().trimmed();
	name.replace("  ", " ");
	univ.replace("  ", " ");
	if(name.isEmpty()) {
		showMsg(_name);
		return;
	}
	if(univ.isEmpty()) {
		showMsg(_university);
		return;
	}
	bool admission = true;
	int year = _yearAdmission->value();
	if(year==0) {
		admission = false;
		year = _yearGraduation->value();
		if(year==0) {
			showMsg(_yearAdmission);
			return;
		}
	}
	QString url = s_checkStudent.arg(univ, name, admission ? "admission" : "graduation").arg(year);
	QDesktopServices::openUrl(url);
}
