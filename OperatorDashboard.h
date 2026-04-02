#pragma once
#include <QWidget>
#include <QComboBox>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include "../models/User.h"
#include "../models/Vendor.h"
#include "../models/MarketDate.h"
#include "../controller/MarketManager.h"

class OperatorDashboard : public QWidget {
    Q_OBJECT
public:
    explicit OperatorDashboard(User* operatorUser, MarketManager* manager,
                                QWidget* parent = nullptr);
    void refresh();

signals:
    void signOut();

private slots:
    void onVendorChanged(int index);
    void onBookOnBehalf();
    void onCancelOnBehalf();
    void onRemoveFromWaitlistOnBehalf();
    void onSelectionChanged();

private:
    void setupUI();
    void populateTable();
    void updateActionButtons();
    MarketDate* getSelectedDate();
    Vendor*     selectedVendor();

    User*          m_operatorUser;
    MarketManager* m_manager;

    QComboBox*    m_vendorPicker;
    QTableWidget* m_table;
    QPushButton*  m_bookBtn;
    QPushButton*  m_cancelBtn;
    QPushButton*  m_removeWaitlistBtn;
    QLabel*       m_statusBar;
    QLabel*       m_vendorInfoLabel;
};