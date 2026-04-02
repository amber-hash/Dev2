#pragma once
#include <QMainWindow>
#include <QStackedWidget>
#include "../models/User.h"
#include "../controller/MarketManager.h"

class LoginScreen;
class VendorDashboard;
class MarketScheduleScreen;
class OperatorDashboard;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onLoginSuccess(User* user);
    void onSignOut();
    void showVendorDashboard();
    void showMarketSchedule();

private:
    QStackedWidget* m_stack;
    MarketManager*  m_manager;     // Single controller instance, passed to all screens

    LoginScreen*          m_loginScreen;
    VendorDashboard*      m_vendorDashboard = nullptr;
    MarketScheduleScreen* m_marketSchedule  = nullptr;
    OperatorDashboard*    m_operatorDashboard  = nullptr; 


    User* m_currentUser = nullptr;
};
