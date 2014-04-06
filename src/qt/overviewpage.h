#ifndef OVERVIEWPAGE_H
#define OVERVIEWPAGE_H

#include <QWidget>
#include <QNetworkReply>

namespace Ui {
    class OverviewPage;
}
class ClientModel;
class WalletModel;
class TxViewDelegate;
class TransactionFilterProxy;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(QWidget *parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    // CryptoMP - Get EUR,USD,BTC Value
    void requestData();
    void showOutOfSyncWarning(bool fShow);

public slots:
    void setBalance(qint64 balance, qint64 unconfirmedBalance, qint64 immatureBalance);
    // CryptoMP - Get EUR,USD,BTC Value
    void requestReceived(QNetworkReply *reply);
    void updateFiat();
    void onPtceur();
    void onPtcusd();
    void onOneday();
    void onSevenday();
    void onFtday();

signals:
    void transactionClicked(const QModelIndex &index);

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    qint64 currentBalance;
    qint64 currentUnconfirmedBalance;
    qint64 currentImmatureBalance;
    // CryptoMP - EUR,USD,BTC Value
    float currentEuroExchange, oldEuroExchange;
    float currentUsdExchange, oldUsdExchange;
    QNetworkAccessManager *m_networkManager;
    QTimer *timer;
    int m_currency;
    int m_interval;

    TxViewDelegate *txdelegate;
    TransactionFilterProxy *filter;

private slots:
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);

};

#endif // OVERVIEWPAGE_H
