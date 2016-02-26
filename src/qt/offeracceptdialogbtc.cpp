#include "offeracceptdialogbtc.h"
#include "ui_offeracceptdialogbtc.h"
#include "init.h"
#include "util.h"
#include "offerpaydialog.h"
#include "syscoingui.h"
#include <QMessageBox>
#include "rpcserver.h"
#include "pubkey.h"
#include "wallet/wallet.h"
#include "main.h"
#include <QDesktopServices>
#include <QUrl>
using namespace std;

extern const CRPCTable tableRPC;
OfferAcceptDialogBTC::OfferAcceptDialogBTC(QString alias, QString offer, QString quantity, QString notes, QString title, QString currencyCode, QString qstrPrice, QString sellerAlias, QString address, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OfferAcceptDialogBTC), alias(alias), offer(offer), notes(notes), quantity(quantity), title(title), sellerAlias(sellerAlias), address(address)
{
    ui->setupUi(this);
	QString theme = GUIUtil::getThemeName();  
	ui->aboutShadeBTC->setPixmap(QPixmap(QString::fromUtf8(strprintf(":/icons/" + theme.toStdString() + "/about_btc"))));
	double dblPrice = qstrPrice.toDouble()*quantity.toUInt();
	string strfPrice = strprintf("%f", dblPrice);
	QString fprice = QString::fromStdString(strfPrice);
	string strCurrencyCode = currencyCode.toStdString();
	ui->escrowDisclaimer->setText(tr("<font color='red'>Please note escrow is not available since you are paying in BTC, only SYS payments can be escrowed. </font>"));
	ui->acceptMessage->setText(tr("Are you sure you want to purchase %1 of '%2' from merchant: '%3'? To complete your purchase please pay %4 BTC using your Bitcoin wallet.").arg(quantity).arg(title).arg(sellerAlias).arg(fprice));
	string strPrice = strprintf("%f", dblPrice);
	price = QString::fromStdString(strPrice);
	
	this->offerPaid = false;
	connect(ui->confirmButton, SIGNAL(clicked()), this, SLOT(acceptPayment()));
	connect(ui->openBtcWalletButton, SIGNAL(clicked()), this, SLOT(openBTCWallet()));

}
void OfferAcceptDialogBTC::on_cancelButton_clicked()
{
    reject();
}
OfferAcceptDialogBTC::~OfferAcceptDialogBTC()
{
    delete ui;
}

void OfferAcceptDialogBTC::acceptPayment()
{
	acceptOffer();
}
// send offeraccept with offer guid/qty as params and then send offerpay with wtxid (first param of response) as param, using RPC commands.
void OfferAcceptDialogBTC::acceptOffer()
{
        if (ui->btctxidEdit->text().trimmed().isEmpty()) {
            ui->btctxidEdit->setText("");
            QMessageBox::information(this, windowTitle(),
            tr("Please enter a valid Bitcoin Transaction ID into the input box and try again"),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
		UniValue params(UniValue::VARR);
		UniValue valError;
		UniValue valResult;
		UniValue valId;
		UniValue result ;
		string strReply;
		string strError;

		string strMethod = string("offeraccept");
		if(this->quantity.toLong() <= 0)
		{
			QMessageBox::critical(this, windowTitle(),
				tr("Invalid quantity when trying to accept offer!"),
				QMessageBox::Ok, QMessageBox::Ok);
			return;
		}
		this->offerPaid = false;
		params.push_back(this->alias.toStdString());
		params.push_back(this->offer.toStdString());
		params.push_back(this->quantity.toStdString());
		params.push_back(this->notes.toStdString());
		params.push_back("");
		params.push_back(ui->btctxidEdit->text().toStdString());

	    try {
            result = tableRPC.execute(strMethod, params);
			if (result.type() != UniValue::VNULL)
			{
				const UniValue &arr = result.get_array();
				string strResult = arr[0].get_str();
				QString offerAcceptTXID = QString::fromStdString(strResult);
				if(offerAcceptTXID != QString(""))
				{
					OfferPayDialog dlg(this->title, this->quantity, this->price, "BTC", this);
					dlg.exec();
					this->offerPaid = true;
					OfferAcceptDialogBTC::accept();
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
void OfferAcceptDialogBTC::openBTCWallet()
{
	QString message = "Payment for offer ID " + this->offer + " on Syscoin Decentralized Marketplace";
	QString qURI = "bitcoin:" + this->address + "?amount=" + price + "&label=" + this->sellerAlias + "&message=" + QUrl::toPercentEncoding(message);
	QDesktopServices::openUrl(QUrl(qURI, QUrl::TolerantMode));
}
bool OfferAcceptDialogBTC::getPaymentStatus()
{
	return this->offerPaid;
}
