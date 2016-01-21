#ifndef OFFERACCEPTDIALOGBTC_H
#define OFFERACCEPTDIALOGBTC_H

#include <QDialog>
namespace Ui {
    class OfferAcceptDialogBTC;
}
class OfferAcceptDialogBTC : public QDialog
{
    Q_OBJECT

public:
    explicit OfferAcceptDialogBTC(QString offer, QString quantity, QString notes, QString title, QString currencyCode, QString strPrice, QString sellerAlias, QString address, QWidget *parent=0);
    ~OfferAcceptDialogBTC();

    bool getPaymentStatus();

private:

    Ui::OfferAcceptDialogBTC *ui;
	QString quantity;
	QString notes;
	QString price;
	QString title;
	QString offer;
	QString sellerAlias;
	QString address;
	bool offerPaid; 
	

private Q_SLOTS:
	void acceptPayment();
	void on_cancelButton_clicked();
    void acceptOffer();
	void openBTCWallet();
};

#endif // OFFERACCEPTDIALOGBTC_H
