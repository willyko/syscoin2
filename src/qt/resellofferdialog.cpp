#include "resellofferdialog.h"
#include "ui_resellofferdialog.h"
#include "offertablemodel.h"
#include "guiutil.h"
#include "syscoingui.h"
#include "ui_interface.h"
#include <QDataWidgetMapper>
#include <QMessageBox>
#include <QStringList>
#include "rpcserver.h"
using namespace std;
extern const CRPCTable tableRPC;
ResellOfferDialog::ResellOfferDialog(QModelIndex *idx, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ResellOfferDialog)
{
    ui->setupUi(this);

	QString offerGUID = idx->data(OfferTableModel::NameRole).toString();
	ui->descriptionEdit->setPlainText(idx->data(OfferTableModel::DescriptionRole).toString());
	ui->offerGUIDLabel->setText(offerGUID);
	ui->commissionDisclaimer->setText(tr("<font color='red'>The payment of <b>commission</b> for an offer sale. Payments will be calculated on the basis of a percentage of the offer value. Enter your desired percentage.</font>"));

}

ResellOfferDialog::~ResellOfferDialog()
{
    delete ui;
}

bool ResellOfferDialog::saveCurrentRow()
{

	UniValue params(UniValue::VARR);
	string strMethod;

	strMethod = string("offerlink");
	params.push_back(ui->offerGUIDLabel->text().toStdString());
	params.push_back(ui->commissionEdit->text().toStdString());
	params.push_back(ui->descriptionEdit->toPlainText().toStdString());

	try {
        UniValue result = tableRPC.execute(strMethod, params);

		QMessageBox::information(this, windowTitle(),
        tr("Offer resold successfully! Check the <b>Selling</b> tab to see it after it has confirmed."),
			QMessageBox::Ok, QMessageBox::Ok);
		return true;	
		
	}
	catch (UniValue& objError)
	{
		string strError = find_value(objError, "message").get_str();
		QMessageBox::critical(this, windowTitle(),
		tr("Error creating new whitelist entry: \"%1\"").arg(QString::fromStdString(strError)),
			QMessageBox::Ok, QMessageBox::Ok);
	}
	catch(std::exception& e)
	{
		QMessageBox::critical(this, windowTitle(),
			tr("General exception creating new whitelist entry: \"%1\"").arg(QString::fromStdString(e.what())),
			QMessageBox::Ok, QMessageBox::Ok);
	}							

    return false;
}

void ResellOfferDialog::accept()
{
    

    if(!saveCurrentRow())
    {
        return;
    }
    QDialog::accept();
}


