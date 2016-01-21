#include "offeracceptdialog.h"
#include "ui_offeracceptdialog.h"
#include "init.h"
#include "util.h"
#include "offerpaydialog.h"
#include "offerescrowdialog.h"
#include "offer.h"
#include "alias.h"
#include "syscoingui.h"
#include <QMessageBox>
#include "rpcserver.h"
#include "pubkey.h"
#include "wallet/wallet.h"
#include "main.h"
using namespace std;

extern const CRPCTable tableRPC;
OfferAcceptDialog::OfferAcceptDialog(QString offer, QString quantity, QString notes, QString title, QString currencyCode, QString qstrPrice, QString sellerAlias, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OfferAcceptDialog), offer(offer), notes(notes), quantity(quantity), title(title)
{
    ui->setupUi(this);
	int precision;
	double dblPrice = qstrPrice.toDouble()*quantity.toUInt();
	string strCurrencyCode = currencyCode.toStdString();
	ui->acceptBtcButton.setEnabled(false);
	ui->acceptBtcButton.setVisible(false);
	CAmount iPrice = convertCurrencyCodeToSyscoin(vchFromString(strCurrencyCode), dblPrice, chainActive.Tip()->nHeight, precision);
	iPrice = ValueFromAmount(iPrice).get_real()*quantity.toUInt();
	string strPrice = strprintf("%llu", iPrice);
	price = QString::fromStdString(strPrice);
	if(strCurrencyCode == "BTC")
	{
		string strfPrice = strprintf("%f", dblPrice);
		QString fprice = QString::fromStdString(strfPrice);
		ui->acceptBtcButton.setEnabled(true);
		ui->acceptBtcButton.setVisible(true);
		ui->escrowDisclaimer->setText(tr("<font color='red'>Select an arbiter that is mutally trusted between yourself and the merchant. Note that escrow is not available if you pay with BTC</font>"));
		ui->acceptMessage->setText(tr("Are you sure you want to purchase %1 of '%2' from merchant: '%3'? You will be charged %4 SYS (%5 BTC)").arg(quantity).arg(title).arg(price).arg(sellerAlias).arg(fprice));
	}
	else
	{
		ui->escrowDisclaimer->setText(tr("<font color='red'>Select an arbiter that is mutally trusted between yourself and the merchant.</font>"));
		ui->acceptMessage->setText(tr("Are you sure you want to purchase %1 of '%2' from merchant: '%3'? You will be charged %3 SYS").arg(quantity).arg(title).arg(sellerAlias).arg(price));
	}
		
	connect(ui->checkBox,SIGNAL(clicked(bool)),SLOT(onEscrowCheckBoxChanged(bool)));
	this->offerPaid = false;
	connect(ui->acceptButton, SIGNAL(clicked()), this, SLOT(acceptPayment()));
	setupEscrowCheckboxState();

}
void OfferAcceptDialog::on_cancelButton_clicked()
{
    reject();
}
OfferAcceptDialog::~OfferAcceptDialog()
{
    delete ui;
}
void OfferAcceptDialog::setupEscrowCheckboxState()
{
	if(ui->checkBox->isChecked())
	{
		ui->escrowDisclaimer->setVisible(true);
		ui->escrowEdit->setEnabled(true);
		ui->acceptButton->setText(tr("Pay Escrow"));
	}
	else
	{
		ui->escrowDisclaimer->setVisible(false);
		ui->escrowEdit->setEnabled(false);
		ui->acceptButton->setText(tr("Pay For Item"));
	}
}
void OfferAcceptDialog::onEscrowCheckBoxChanged(bool toggled)
{
	setupEscrowCheckboxState();
}
void OfferAcceptDialog::acceptPayment()
{
	if(ui->checkBox->isChecked())
		acceptEscrow();
	else
		acceptOffer();
}
// send offeraccept with offer guid/qty as params and then send offerpay with wtxid (first param of response) as param, using RPC commands.
void OfferAcceptDialog::acceptOffer()
{

		UniValue params(UniValue::VARR);
		UniValue valError;
		UniValue valResult;
		UniValue valId;
		UniValue result ;
		string strReply;
		string strError;
  		CPubKey newDefaultKey;
		pwalletMain->GetKeyFromPool(newDefaultKey); 
		std::vector<unsigned char> vchPubKey(newDefaultKey.begin(), newDefaultKey.end());
		string strPubKey = HexStr(vchPubKey);

		string strMethod = string("offeraccept");
		if(this->quantity.toLong() <= 0)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Invalid quantity when trying to accept offer!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		this->offerPaid = false;
		params.push_back(this->offer.toStdString());
		params.push_back(this->quantity.toStdString());
		params.push_back(strPubKey);
		params.push_back(this->notes.toStdString());
		

	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() != UniValue::VNULL)
			{
				const UniValue &arr = result.get_array();
				string strResult = arr[0].get_str();
				QString offerAcceptTXID = QString::fromStdString(strResult);
				if(offerAcceptTXID != QString(""))
				{
					OfferPayDialog dlg(this->title, this->quantity, this->price, "SYS", this);
					dlg.exec();
					this->offerPaid = true;
					OfferAcceptDialog::accept();
					return;

				}
			}
		}
		catch (UniValue& objError)
		{
			strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
			tr("Error accepting offer: \"%1\"").arg(QString::fromStdString(strError)),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("General exception when accepting offer"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
	
   

}
void OfferAcceptDialog::acceptEscrow()
{

		UniValue params(UniValue::VARR);
		UniValue valError;
		UniValue valResult;
		UniValue valId;
		UniValue result ;
		string strReply;
		string strError;

		string strMethod = string("escrownew");
		if(this->quantity.toLong() <= 0)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Invalid quantity when trying to create escrow!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		this->offerPaid = false;
		params.push_back(this->offer.toStdString());
		params.push_back(this->quantity.toStdString());
		params.push_back(this->notes.toStdString());
		params.push_back(ui->escrowEdit->text().toStdString());

	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() != UniValue::VNULL)
			{
				const UniValue &arr = result.get_array();
				string strResult = arr[0].get_str();
				QString escrowTXID = QString::fromStdString(strResult);
				if(escrowTXID != QString(""))
				{
					OfferEscrowDialog dlg(this->title, this->quantity, this->price, this);
					dlg.exec();
					this->offerPaid = true;
					OfferAcceptDialog::accept();
					return;

				}
			}
		}
		catch (UniValue& objError)
		{
			strError = find_value(objError, "message").get_str();
			QMessageBox::critical(this, windowTitle(),
			tr("Error creating escrow: \"%1\"").arg(QString::fromStdString(strError)),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		catch(std::exception& e)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("General exception when creating escrow"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
	
   

}
bool OfferAcceptDialog::getPaymentStatus()
{
	return this->offerPaid;
}
