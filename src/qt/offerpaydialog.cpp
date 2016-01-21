#include "init.h"
#include "util.h"
#include "offerpaydialog.h"
#include "ui_offerpaydialog.h"
OfferPayDialog::OfferPayDialog(QString title, QString quantity, QString price, QString currency,QWidget *parent) :
    QDialog(parent), 
	ui(new Ui::OfferPayDialog)
{
    ui->setupUi(this);
	connect(ui->finishButton, SIGNAL(clicked()), this, SLOT(accept()));
	ui->payMessage->setText(tr("<p>You've purchased %1 of '%2' for %3 %4!</p><p><FONT COLOR='green'><b>Your payment is complete!</b></FONT></p><p>The merchant has been sent your delivery information and your item should arrive shortly. The merchant may followup with further information through a private message (please check your inbox regularily).</p><br>").arg(quantity).arg(title).arg(price).arg(currency));			
	ui->purchaseHint->setText(tr("Please click Finish"));

}

OfferPayDialog::~OfferPayDialog()
{
    delete ui;
}


