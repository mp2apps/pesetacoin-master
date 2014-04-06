#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "clientmodel.h"
#include "walletmodel.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "transactionfilterproxy.h"
#include "guiutil.h"
#include "guiconstants.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDebug>
#include <QTimer>


#define DECORATION_SIZE 64
#define NUM_ITEMS 3

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(): QAbstractItemDelegate(), unit(BitcoinUnits::BTC)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    txdelegate(new TxViewDelegate()),
    filter(0)
{
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);

    // CryptoMP - Get Values
    m_currency=0;
    m_interval=0;
    requestData();
    // CryptoMP - Set Ticker Update Timer (5 Minutes)
    timer = new QTimer(this);
         connect(timer, SIGNAL(timeout()), this, SLOT(updateFiat()));
         timer->start(300000);

    // CryptoMP - Connect Buttons
    QObject::connect(ui->bptceur, SIGNAL(clicked()), this, SLOT(onPtceur()));
    QObject::connect(ui->bptcusd, SIGNAL(clicked()), this, SLOT(onPtcusd()));
    QObject::connect(ui->boneday, SIGNAL(clicked()), this, SLOT(onOneday()));
    QObject::connect(ui->bsevenday, SIGNAL(clicked()), this, SLOT(onSevenday()));
    QObject::connect(ui->bftday, SIGNAL(clicked()), this, SLOT(onFtday()));
}

void OverviewPage::onPtceur()
{
    m_currency = 0;
    requestData();
}

void OverviewPage::onPtcusd()
{
    m_currency = 1;
    requestData();
}

void OverviewPage::onOneday()
{
    m_interval = 0;
    requestData();
}

void OverviewPage::onSevenday()
{
    m_interval = 1;
    requestData();
}

void OverviewPage::onFtday()
{
    m_interval = 2;
    requestData();
}


void OverviewPage::updateFiat()
{
    requestData();
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    timer->stop();
    delete timer;
    delete ui;
}

// CryptoMP - Get EUR, USD and BTC Value
void OverviewPage::requestData(){
    QNetworkRequest request;
    QString theURL;
    theURL.sprintf("http://www.pesetacoin.info/graph-wallet-new.php?mkt=%d&int=%d", m_currency, m_interval);
    request.setUrl(QUrl(theURL));
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager,SIGNAL(finished(QNetworkReply*)),this,
            SLOT(requestReceived(QNetworkReply*)));

    // store actual values
    oldEuroExchange = currentEuroExchange;
    oldUsdExchange = currentUsdExchange;
    // request data
    m_networkManager->get(request);
}

// CryptoMP - On received data
void OverviewPage::requestReceived(QNetworkReply *reply){

    if (reply->error()) {
        ui->labelFiatlbl->setText("Error");
    }
    else {
        QByteArray bytes = reply->readAll();
        QString replyText (bytes);

        QStringList values = replyText.split("[]");
        QStringList last =  replyText.split("[v]");
        QString euro = last.value( 1 );
        QString usd = last.value( 2 );

        currentEuroExchange = euro.toFloat();
        currentUsdExchange = usd.toFloat();

        // uptrend or downtrend?? if equal no color change...
        if (currentEuroExchange > oldEuroExchange)
            ui->labelFiatlbl->setStyleSheet("QLabel { color : green; }");
        else if (currentEuroExchange < oldEuroExchange)
            ui->labelFiatlbl->setStyleSheet("QLabel { color : red; }");

        // CryptoMP - FIAT Values Update
        QString OutputStr;
        float fullBalance = currentBalance / 100000000;
        OutputStr.sprintf("%.2f€/$%.2f", (fullBalance * currentEuroExchange), (fullBalance * currentUsdExchange));
        ui->labelFiatlbl->setText(OutputStr);

        QDateTime datetime = QDateTime::currentDateTime();
        uint theDate = datetime.toTime_t();

       float lmin=1, lmax=0;
        // Draw Plot
        QVector<double> x(720), y(720);

        if (m_interval == 0) {

            for (int i=0; i<719; i++)
            {
            x[i] = theDate - ((720-i)*120);
            QString euroVal = values.value( i+1 );
            y[i] = euroVal.toFloat();

            if (lmax < y[i]) lmax = y[i];
            if (lmin > y[i]) lmin = y[i];
            }
        }
        else if (m_interval == 1) {

            for (int i=0; i<499; i++)
            {
            x[i] = theDate - ((500-i)*1200);
            QString euroVal = values.value( i+1 );
            y[i] = euroVal.toFloat();

            if (lmax < y[i]) lmax = y[i];
            if (lmin > y[i]) lmin = y[i];
            }
        }
        else if (m_interval == 2) {

            for (int i=0; i<499; i++)
            {
            x[i] = theDate - ((500-i)*2400);
            QString euroVal = values.value( i+1 );
            y[i] = euroVal.toFloat();

            if (lmax < y[i]) lmax = y[i];
            if (lmin > y[i]) lmin = y[i];
            }
        }

        // create graph and assign data to it:
        QLinearGradient lgrad(ui->customPlot->rect().topLeft(),ui->customPlot->rect().bottomLeft());
        lgrad.setColorAt(0.0, QColor(255,80,0));
        lgrad.setColorAt(1.0, QColor(255,150,0));

        ui->customPlot->addGraph();
        ui->customPlot->graph(0)->setData(x,y);
        ui->customPlot->graph(0)->setPen(QPen(Qt::red, 2));
        ui->customPlot->graph(0)->setBrush(QBrush(lgrad));
        // give the axes some labels:
        //ui->customPlot->xAxis->setLabel("Valor Ultimos 7 Días / Last 7 Days Value");

        if (m_currency == 0)
            ui->customPlot->yAxis->setLabel("PTC/EUR");
        else
            ui->customPlot->yAxis->setLabel("PTC/USD");
        // set axes ranges, so we see all data:
        // configure bottom axis to show date and time instead of number:
        ui->customPlot->xAxis->setTickLabelType(QCPAxis::ltDateTime);
        ui->customPlot->xAxis->setDateTimeFormat("dd/MMM");


        if (m_interval == 0) {
            ui->customPlot->xAxis->setRange(theDate-86400, theDate);
            ui->customPlot->xAxis->setDateTimeFormat("hh:mm (ddd)");
        }
        else if (m_interval == 1)
            ui->customPlot->xAxis->setRange(theDate-600000, theDate);
        else if (m_interval == 2)
            ui->customPlot->xAxis->setRange(theDate-1200000, theDate);

        ui->customPlot->yAxis->setRange(lmin, lmax);


        ui->customPlot->setBackground( QBrush((QColor(240,240,240))) );
        ui->customPlot->replot();

        // Cleanup
        reply->deleteLater();
    }
}



void OverviewPage::setBalance(qint64 balance, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance));
    // CryptoMP - FIAT Values - Update at balance change
    QString OutputStr;
    float fullBalance = currentBalance / 100000000;
    OutputStr.sprintf("%.2f€/$%.2f", (fullBalance * currentEuroExchange), (fullBalance * currentUsdExchange));
    ui->labelFiatlbl->setText(OutputStr);


    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    ui->labelImmature->setVisible(showImmature);
    ui->labelImmatureText->setVisible(showImmature);
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);


        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}
