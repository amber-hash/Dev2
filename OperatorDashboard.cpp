#include "OperatorDashboard.h"
#include "StyleSheet.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QHeaderView>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QScrollArea>

OperatorDashboard::OperatorDashboard(User* operatorUser, MarketManager* manager,
                                      QWidget* parent)
    : QWidget(parent), m_operatorUser(operatorUser), m_manager(manager) {
    setupUI();
}

void OperatorDashboard::setupUI() {
    setStyleSheet(StyleSheet::global());

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ── Toolbar — uses a distinct amber/gold colour to distinguish from vendor UI ──
    QFrame* toolbar = new QFrame();
    toolbar->setStyleSheet("QFrame { background: #7A5C3A; border: none; }");
    toolbar->setFixedHeight(56);
    QHBoxLayout* tbl = new QHBoxLayout(toolbar);
    tbl->setContentsMargins(20, 0, 20, 0);

    QLabel* appName = new QLabel("🌿 HintonMarket  —  Operator Console");
    appName->setStyleSheet("color: #F8F3E8; font-size: 18px; font-weight: bold; font-family: Georgia, serif;");

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QLabel* userLabel = new QLabel("Market Operator");
    userLabel->setStyleSheet("color: #E8D4B0; font-size: 12px;");

    QPushButton* signOutBtn = new QPushButton("Sign Out");
    signOutBtn->setStyleSheet(R"(
        QPushButton { background: transparent; color: #F8F3E8; border: 1px solid #C4A882;
                      border-radius: 4px; padding: 6px 14px; font-family: Georgia, serif; font-size: 12px; }
        QPushButton:hover { background: rgba(255,255,255,0.1); }
    )");
    signOutBtn->setCursor(Qt::PointingHandCursor);
    connect(signOutBtn, &QPushButton::clicked, this, &OperatorDashboard::signOut);

    tbl->addWidget(appName);
    tbl->addWidget(spacer);
    tbl->addWidget(userLabel);
    tbl->addSpacing(16);
    tbl->addWidget(signOutBtn);
    outerLayout->addWidget(toolbar);

    // ── Operator badge banner ─────────────────────────────────────────────────
    QFrame* banner = new QFrame();
    banner->setStyleSheet("QFrame { background: #FFF3CD; border-bottom: 1px solid #E8C84C; border: none; }");
    banner->setFixedHeight(36);
    QHBoxLayout* bl = new QHBoxLayout(banner);
    bl->setContentsMargins(24, 0, 24, 0);
    QLabel* bannerLbl = new QLabel(
        "⚙️  You are acting as Market Operator. Actions below are performed on behalf of vendors.");
    bannerLbl->setStyleSheet("color: #7A5C10; font-size: 12px; font-family: Georgia, serif;");
    bl->addWidget(bannerLbl);
    outerLayout->addWidget(banner);

    // ── Content ───────────────────────────────────────────────────────────────
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: #FDFAF4; }");

    QWidget* content = new QWidget();
    content->setStyleSheet("background: #FDFAF4;");
    QVBoxLayout* cl = new QVBoxLayout(content);
    cl->setContentsMargins(32, 28, 32, 28);
    cl->setSpacing(18);

    QLabel* pageTitle = new QLabel("Manage Stall Bookings on Behalf of Vendors");
    pageTitle->setStyleSheet("font-size: 22px; font-weight: bold; color: #2B2016; font-family: Georgia, serif;");
    cl->addWidget(pageTitle);

    // ── Vendor selector ───────────────────────────────────────────────────────
    QFrame* vendorCard = new QFrame();
    vendorCard->setStyleSheet(StyleSheet::card());
    QVBoxLayout* vcl = new QVBoxLayout(vendorCard);
    vcl->setContentsMargins(20, 16, 20, 16);
    vcl->setSpacing(10);

    QLabel* selectorTitle = new QLabel("Select Vendor");
    selectorTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #3D6B4F; font-family: Georgia, serif;");
    vcl->addWidget(selectorTitle);

    QFrame* div = new QFrame();
    div->setFrameShape(QFrame::HLine);
    div->setStyleSheet("background: #D9CDB8; border: none; max-height: 1px;");
    vcl->addWidget(div);

    QHBoxLayout* pickerRow = new QHBoxLayout();
    QLabel* pickerLbl = new QLabel("Vendor:");
    pickerLbl->setStyleSheet("color: #7A5C3A; font-size: 13px; min-width: 60px;");

    m_vendorPicker = new QComboBox();
    m_vendorPicker->setStyleSheet(R"(
        QComboBox {
            background: #FDFAF4; border: 1.5px solid #C4A882; border-radius: 5px;
            padding: 6px 12px; font-family: Georgia, serif; font-size: 13px; color: #2B2016;
        }
        QComboBox:focus { border-color: #3D6B4F; }
        QComboBox::drop-down { border: none; width: 24px; }
        QComboBox QAbstractItemView {
            background: #FDFAF4; border: 1px solid #C4A882; selection-background-color: #D4E8DA;
        }
    )");
    m_vendorPicker->setCursor(Qt::PointingHandCursor);

    // Populate vendor list
    QList<Vendor*> vendors = m_manager->getAllVendors();
    for (Vendor* v : vendors) {
        m_vendorPicker->addItem(
            v->getBusinessName() + "  (" + v->getCategoryString() + ")",
            QVariant::fromValue(static_cast<void*>(v))
        );
    }

    connect(m_vendorPicker, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OperatorDashboard::onVendorChanged);

    pickerRow->addWidget(pickerLbl);
    pickerRow->addWidget(m_vendorPicker, 1);
    vcl->addLayout(pickerRow);

    m_vendorInfoLabel = new QLabel();
    m_vendorInfoLabel->setStyleSheet("color: #5C4A30; font-size: 12px; padding-top: 4px;");
    vcl->addWidget(m_vendorInfoLabel);

    cl->addWidget(vendorCard);

    // ── Schedule table ────────────────────────────────────────────────────────
    QLabel* tableTitle = new QLabel("Market Schedule");
    tableTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #3D6B4F; font-family: Georgia, serif;");
    cl->addWidget(tableTitle);

    m_table = new QTableWidget();
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({
        "Market Date", "Availability", "Status", "Vendor Booking", "Waitlist"
    });
    m_table->setStyleSheet(StyleSheet::tableWidget() + "QTableWidget { alternate-background-color: #F5F0E8; }");
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &OperatorDashboard::onSelectionChanged);
    cl->addWidget(m_table);

    // ── Action bar ────────────────────────────────────────────────────────────
    QFrame* actionBar = new QFrame();
    actionBar->setStyleSheet("QFrame { background: #FAF7F0; border: 1px solid #D9CDB8; border-radius: 6px; }");
    QHBoxLayout* al = new QHBoxLayout(actionBar);
    al->setContentsMargins(16, 12, 16, 12);
    al->setSpacing(12);

    QLabel* actionLabel = new QLabel("Select a row, then act on behalf of the vendor:");
    actionLabel->setStyleSheet("color: #7A5C3A; font-size: 12px;");

    m_bookBtn = new QPushButton("📋  Book Stall");
    m_bookBtn->setStyleSheet(StyleSheet::primaryButton());
    m_bookBtn->setCursor(Qt::PointingHandCursor);
    m_bookBtn->setEnabled(false);
    connect(m_bookBtn, &QPushButton::clicked, this, &OperatorDashboard::onBookOnBehalf);

    m_cancelBtn = new QPushButton("✗  Cancel Booking");
    m_cancelBtn->setStyleSheet(StyleSheet::dangerButton());
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &OperatorDashboard::onCancelOnBehalf);

    m_removeWaitlistBtn = new QPushButton("✗  Remove from Waitlist");
    m_removeWaitlistBtn->setStyleSheet(StyleSheet::dangerButton());
    m_removeWaitlistBtn->setCursor(Qt::PointingHandCursor);
    m_removeWaitlistBtn->setEnabled(false);
    connect(m_removeWaitlistBtn, &QPushButton::clicked,
            this, &OperatorDashboard::onRemoveFromWaitlistOnBehalf);

    al->addWidget(actionLabel);
    al->addStretch();
    al->addWidget(m_bookBtn);
    al->addWidget(m_cancelBtn);
    al->addWidget(m_removeWaitlistBtn);
    cl->addWidget(actionBar);

    m_statusBar = new QLabel("");
    m_statusBar->setStyleSheet("font-size: 12px; color: #5C4A30; padding: 4px 0;");
    cl->addWidget(m_statusBar);
    cl->addStretch();

    scroll->setWidget(content);
    outerLayout->addWidget(scroll);

    // Initial population
    populateTable();
    updateVendorInfo();
}

void OperatorDashboard::updateVendorInfo() {
    Vendor* v = selectedVendor();
    if (!v) {
        m_vendorInfoLabel->setText("");
        return;
    }
    bool compliant = m_manager->vendorIsCompliant(v);
    QString info = QString("Owner: %1  |  Email: %2  |  Compliance: %3")
        .arg(v->getOwnerName())
        .arg(v->getEmail())
        .arg(compliant ? "✅ All documents on file" : "⚠️ Incomplete documents");
    m_vendorInfoLabel->setText(info);
}

Vendor* OperatorDashboard::selectedVendor() {
    if (m_vendorPicker->currentIndex() < 0) return nullptr;
    return static_cast<Vendor*>(
        m_vendorPicker->currentData().value<void*>()
    );
}

MarketDate* OperatorDashboard::getSelectedDate() {
    int row = m_table->currentRow();
    if (row < 0) return nullptr;
    QTableWidgetItem* item = m_table->item(row, 0);
    if (!item) return nullptr;
    return static_cast<MarketDate*>(item->data(Qt::UserRole).value<void*>());
}

void OperatorDashboard::populateTable() {
    Vendor* vendor = selectedVendor();
    QList<MarketDate*> dates = m_manager->getFourWeekSchedule();
    m_table->setRowCount(dates.size());

    for (int i = 0; i < dates.size(); ++i) {
        MarketDate* md = dates[i];

        bool isFood  = vendor && vendor->getCategory() == VendorCategory::Food;
        int  avail   = vendor ? (isFood ? md->getAvailableFood() : md->getAvailableArtisan()) : 0;
        int  total   = vendor ? (isFood ? md->getMaxFood()       : md->getMaxArtisan())       : 0;

        bool hasBooking = vendor && m_manager->hasBooking(vendor, md);
        bool onWaitlist = vendor && m_manager->isOnWaitlist(vendor, md);
        int  waitPos    = vendor ? m_manager->getWaitlistPosition(vendor, md) : -1;
        bool hasOther   = vendor && !hasBooking && m_manager->hasActiveBooking(vendor);

        // Col 0: Date
        QTableWidgetItem* dateItem = new QTableWidgetItem("  " + md->getDateString());
        dateItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(md)));
        m_table->setItem(i, 0, dateItem);

        // Col 1: Availability
        QString availStr = vendor
            ? QString("%1 / %2 %3 stalls").arg(avail).arg(total).arg(isFood ? "food" : "artisan")
            : "—";
        QTableWidgetItem* availItem = new QTableWidgetItem(availStr);
        availItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 1, availItem);

        // Col 2: Status
        QString statusText;
        QColor  statusColor;
        if (!vendor) {
            statusText = "Select a vendor"; statusColor = QColor("#888888");
        } else if (hasBooking) {
            statusText = "Booked"; statusColor = QColor("#2060C0");
        } else if (hasOther) {
            statusText = "1 Booking Limit"; statusColor = QColor("#888888");
        } else if (avail > 0) {
            statusText = "Available"; statusColor = QColor("#3D6B4F");
        } else {
            statusText = "Waitlist Only"; statusColor = QColor("#C47C20");
        }
        QTableWidgetItem* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(statusColor);
        statusItem->setFont(QFont("Georgia", 12, QFont::Bold));
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 2, statusItem);

        // Col 3: Vendor booking
        QTableWidgetItem* bookItem = new QTableWidgetItem(hasBooking ? "✅  Booked" : "—");
        bookItem->setTextAlignment(Qt::AlignCenter);
        if (hasBooking) bookItem->setForeground(QColor("#2060C0"));
        m_table->setItem(i, 3, bookItem);

        // Col 4: Waitlist
        QString waitText = onWaitlist ? QString("Position #%1").arg(waitPos) : "—";
        QTableWidgetItem* waitItem = new QTableWidgetItem(waitText);
        waitItem->setTextAlignment(Qt::AlignCenter);
        if (onWaitlist) waitItem->setForeground(QColor("#C47C20"));
        m_table->setItem(i, 4, waitItem);

        m_table->setRowHeight(i, 42);
    }
}

void OperatorDashboard::updateActionButtons() {
    Vendor*     vendor = selectedVendor();
    MarketDate* md     = getSelectedDate();

    if (!vendor || !md) {
        m_bookBtn->setEnabled(false);
        m_cancelBtn->setEnabled(false);
        m_removeWaitlistBtn->setEnabled(false);
        m_statusBar->setText(!vendor ? "Select a vendor to begin." : "Select a market date row.");
        return;
    }

    bool isFood      = vendor->getCategory() == VendorCategory::Food;
    int  avail       = isFood ? md->getAvailableFood() : md->getAvailableArtisan();
    bool hasBooking  = m_manager->hasBooking(vendor, md);
    bool onWaitlist  = m_manager->isOnWaitlist(vendor, md);
    bool hasOther    = !hasBooking && m_manager->hasActiveBooking(vendor);

    m_bookBtn->setEnabled(avail > 0 && !hasBooking && !hasOther);
    m_cancelBtn->setEnabled(hasBooking);
    m_removeWaitlistBtn->setEnabled(onWaitlist);

    QString status;
    if (hasBooking)
        status = QString("✅  %1 has a booking for this date. You may cancel it on their behalf.")
                 .arg(vendor->getBusinessName());
    else if (hasOther)
        status = QString("ℹ  %1 already has an active booking on another date.")
                 .arg(vendor->getBusinessName());
    else if (onWaitlist)
        status = QString("⏳  %1 is on the waitlist (position #%2). You may remove them.")
                 .arg(vendor->getBusinessName())
                 .arg(m_manager->getWaitlistPosition(vendor, md));
    else if (avail > 0)
        status = QString("📋  %1 stall(s) available — you may book on behalf of %2.")
                 .arg(avail).arg(vendor->getBusinessName());
    else
        status = QString("⏳  No stalls available for this date for %1 vendors.")
                 .arg(isFood ? "Food" : "Artisan");

    m_statusBar->setText(status);
}

void OperatorDashboard::onVendorChanged(int /*index*/) {
    populateTable();
    updateActionButtons();
    updateVendorInfo();
}

void OperatorDashboard::onSelectionChanged() {
    updateActionButtons();
}

void OperatorDashboard::onBookOnBehalf() {
    Vendor*     vendor = selectedVendor();
    MarketDate* md     = getSelectedDate();
    if (!vendor || !md) return;

    QMessageBox confirm(this);
    confirm.setWindowTitle("Confirm Booking");
    confirm.setText("<b>Book Stall on Behalf of Vendor</b>");
    confirm.setInformativeText(
        QString("Book a %1 vendor stall for <b>%2</b><br>on behalf of <b>%3</b>?")
        .arg(vendor->getCategoryString()).arg(md->getDateString()).arg(vendor->getBusinessName()));
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    confirm.setStyleSheet(StyleSheet::global());

    if (confirm.exec() == QMessageBox::Yes) {
        if (m_manager->operatorBookStall(vendor, md)) {
            QMessageBox::information(this, "Booking Confirmed",
                QString("✅ Stall booked for <b>%1</b> on <b>%2</b>.<br>"
                        "The vendor will see this reflected in their dashboard.")
                .arg(vendor->getBusinessName()).arg(md->getDateString()));
        } else {
            QString reason;
            if (m_manager->hasActiveBooking(vendor))
                reason = vendor->getBusinessName() + " already has an active booking on another date.";
            else
                reason = "The stall is no longer available for this date.";
            QMessageBox::warning(this, "Booking Failed", reason);
        }
        refresh();
    }
}

void OperatorDashboard::onCancelOnBehalf() {
    Vendor*     vendor = selectedVendor();
    MarketDate* md     = getSelectedDate();
    if (!vendor || !md) return;

    QMessageBox confirm(this);
    confirm.setWindowTitle("Cancel Booking");
    confirm.setText("<b>Cancel Booking on Behalf of Vendor</b>");
    confirm.setInformativeText(
        QString("Cancel the booking for <b>%1</b> on <b>%2</b>?<br>This cannot be undone.")
        .arg(vendor->getBusinessName()).arg(md->getDateString()));
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.setStyleSheet(StyleSheet::global());

    if (confirm.exec() == QMessageBox::Yes) {
        if (m_manager->operatorCancelBooking(vendor, md)) {
            QMessageBox::information(this, "Booking Cancelled",
                QString("❌ Booking for <b>%1</b> on <b>%2</b> has been cancelled.")
                .arg(vendor->getBusinessName()).arg(md->getDateString()));
        } else {
            QMessageBox::warning(this, "Error", "Unable to cancel booking.");
        }
        refresh();
    }
}

void OperatorDashboard::onRemoveFromWaitlistOnBehalf() {
    Vendor*     vendor = selectedVendor();
    MarketDate* md     = getSelectedDate();
    if (!vendor || !md) return;

    QMessageBox confirm(this);
    confirm.setWindowTitle("Remove from Waitlist");
    confirm.setText("<b>Remove Vendor from Waitlist</b>");
    confirm.setInformativeText(
        QString("Remove <b>%1</b> from the waitlist for <b>%2</b>?")
        .arg(vendor->getBusinessName()).arg(md->getDateString()));
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    confirm.setStyleSheet(StyleSheet::global());

    if (confirm.exec() == QMessageBox::Yes) {
        if (m_manager->operatorRemoveFromWaitlist(vendor, md)) {
            QMessageBox::information(this, "Removed from Waitlist",
                QString("🚫 <b>%1</b> has been removed from the waitlist for <b>%2</b>.")
                .arg(vendor->getBusinessName()).arg(md->getDateString()));
        } else {
            QMessageBox::warning(this, "Error", "Unable to remove from waitlist.");
        }
        refresh();
    }
}

void OperatorDashboard::refresh() {
    populateTable();
    updateActionButtons();
    updateVendorInfo();
}