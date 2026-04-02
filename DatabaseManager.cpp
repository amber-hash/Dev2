#include "DatabaseManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QDate>
#include <QDebug>
#include <QVariant>

// ── Singleton ─────────────────────────────────────────────────────────────────

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager mgr;
    return mgr;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool DatabaseManager::open() {
    // Determine path: same directory as the executable so the TA never needs
    // to touch file paths.
    QString dbPath = QCoreApplication::applicationDirPath()
                     + QDir::separator()
                     + "hintonMarket.sqlite3";

    bool dbExists = QFile::exists(dbPath);

    m_db = QSqlDatabase::addDatabase("QSQLITE", "hintonmarket_conn");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qCritical() << "[DB] Failed to open database:" << m_db.lastError().text();
        return false;
    }

    // Enable WAL mode and foreign keys for better performance and correctness
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL;");
    pragma.exec("PRAGMA foreign_keys=ON;");

    if (!dbExists) {
        // Fresh install — create schema and populate default data
        qDebug() << "[DB] New database created at:" << dbPath;
        if (!initializeSchema()) {
            qCritical() << "[DB] Failed to initialize schema.";
            return false;
        }
    } else {
        qDebug() << "[DB] Existing database loaded from:" << dbPath;
    }

    return true;
}

void DatabaseManager::close() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    QSqlDatabase::removeDatabase("hintonmarket_conn");
}

bool DatabaseManager::isOpen() const {
    return m_db.isOpen();
}

// ── Schema & Seeding ──────────────────────────────────────────────────────────

bool DatabaseManager::initializeSchema() {
    if (!createTables()) return false;
    if (!seedUsers())    return false;
    if (!seedMarketDates()) return false;
    return true;
}

bool DatabaseManager::createTables() {
    QSqlQuery q(m_db);

    // ── Users table ──────────────────────────────────────────────────────────
    // Stores all user types: Vendor, MarketOperator, SystemAdmin.
    // Vendor-specific columns are NULL for non-vendor rows.
    if (!q.exec(
        "CREATE TABLE IF NOT EXISTS Users ("
        "  user_id          INTEGER PRIMARY KEY,"
        "  username         TEXT    NOT NULL UNIQUE,"
        "  display_name     TEXT    NOT NULL,"
        "  role             TEXT    NOT NULL,"   // 'Vendor', 'MarketOperator', 'SystemAdmin'
        "  vendor_category  TEXT,"               // 'Food' or 'Artisan' (NULL for non-vendors)
        "  business_name    TEXT,"
        "  owner_name       TEXT,"
        "  email            TEXT,"
        "  phone            TEXT,"
        "  address          TEXT"
        ");"
    )) {
        qCritical() << "[DB] createTables (Users):" << q.lastError().text();
        return false;
    }

    // ── ComplianceDocs table ─────────────────────────────────────────────────
    // Each compliance document belongs to one vendor (user_id).
    if (!q.exec(
        "CREATE TABLE IF NOT EXISTS ComplianceDocs ("
        "  doc_id      INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id     INTEGER NOT NULL,"
        "  doc_type    TEXT    NOT NULL,"   // 'BusinessLicence', 'LiabilityInsurance', 'FoodHandlerCert'
        "  doc_number  TEXT    NOT NULL,"
        "  expiry_date TEXT    NOT NULL,"   // ISO 8601: YYYY-MM-DD
        "  provider    TEXT,"               // insurance provider (nullable)
        "  FOREIGN KEY (user_id) REFERENCES Users(user_id)"
        ");"
    )) {
        qCritical() << "[DB] createTables (ComplianceDocs):" << q.lastError().text();
        return false;
    }

    // ── MarketSchedule table ─────────────────────────────────────────────────
    // One row per market date. max_food/max_artisan cap stall availability.
    // booked counts are derived at load time by counting Bookings rows —
    // keeping booked counts here would risk double-counting bugs, so we
    // intentionally omit them from the schema.
    if (!q.exec(
        "CREATE TABLE IF NOT EXISTS MarketSchedule ("
        "  market_id      INTEGER PRIMARY KEY,"
        "  week_number    INTEGER NOT NULL,"
        "  market_date    TEXT    NOT NULL,"   // ISO 8601: YYYY-MM-DD
        "  max_food       INTEGER NOT NULL DEFAULT 2,"
        "  max_artisan    INTEGER NOT NULL DEFAULT 2"
        ");"
    )) {
        qCritical() << "[DB] createTables (MarketSchedule):" << q.lastError().text();
        return false;
    }

    // ── Bookings table ───────────────────────────────────────────────────────
    // One row per active booking. Cancellation = DELETE from this table.
    if (!q.exec(
        "CREATE TABLE IF NOT EXISTS Bookings ("
        "  booking_id           INTEGER PRIMARY KEY,"
        "  vendor_id            INTEGER NOT NULL,"
        "  market_id            INTEGER NOT NULL,"
        "  market_date          TEXT    NOT NULL,"   // denormalized for fast display
        "  confirmation_number  TEXT    NOT NULL,"
        "  FOREIGN KEY (vendor_id)  REFERENCES Users(user_id),"
        "  FOREIGN KEY (market_id)  REFERENCES MarketSchedule(market_id),"
        "  UNIQUE (vendor_id, market_id)"            // prevent duplicate bookings
        ");"
    )) {
        qCritical() << "[DB] createTables (Bookings):" << q.lastError().text();
        return false;
    }

    // ── Waitlists table ──────────────────────────────────────────────────────
    // FIFO is maintained by entry_order (AUTOINCREMENT). The 8 logical queues
    // are partitioned by (market_id, category) at query time — exactly matching
    // the business rule: 4 weeks × 2 categories = 8 active waitlists.
    //
    // To get the FIFO-ordered queue for week W, category C:
    //   SELECT * FROM Waitlists
    //   WHERE market_id = W AND category = C
    //   ORDER BY entry_order ASC;
    if (!q.exec(
        "CREATE TABLE IF NOT EXISTS Waitlists ("
        "  entry_id      INTEGER PRIMARY KEY,"
        "  entry_order   INTEGER NOT NULL UNIQUE,"   // FIFO guarantee; auto-assigned
        "  vendor_id     INTEGER NOT NULL,"
        "  market_id     INTEGER NOT NULL,"
        "  market_date   TEXT    NOT NULL,"           // denormalized for display
        "  category      TEXT    NOT NULL,"           // 'Food' or 'Artisan'
        "  notified      INTEGER NOT NULL DEFAULT 0," // 0=pending, 1=notified
        "  FOREIGN KEY (vendor_id)  REFERENCES Users(user_id),"
        "  FOREIGN KEY (market_id)  REFERENCES MarketSchedule(market_id),"
        "  UNIQUE (vendor_id, market_id)"             // vendor can only queue once per date
        ");"
    )) {
        qCritical() << "[DB] createTables (Waitlists):" << q.lastError().text();
        return false;
    }

    qDebug() << "[DB] All tables created successfully.";
    return true;
}

// ── Default Data Seeding ──────────────────────────────────────────────────────

bool DatabaseManager::seedUsers() {
    // All inserts wrapped in a transaction for speed and atomicity
    m_db.transaction();

    QSqlQuery q(m_db);

    // Helper lambda to insert a user row
    auto insertUser = [&](int id, const QString& username, const QString& displayName,
                          const QString& role, const QString& category,
                          const QString& businessName, const QString& ownerName,
                          const QString& email, const QString& phone,
                          const QString& address) -> bool {
        q.prepare(
            "INSERT INTO Users "
            "(user_id, username, display_name, role, vendor_category, "
            " business_name, owner_name, email, phone, address) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"
        );
        q.addBindValue(id);
        q.addBindValue(username);
        q.addBindValue(displayName);
        q.addBindValue(role);
        q.addBindValue(category.isEmpty() ? QVariant(QVariant::String) : QVariant(category));
        q.addBindValue(businessName.isEmpty() ? QVariant(QVariant::String) : QVariant(businessName));
        q.addBindValue(ownerName.isEmpty()    ? QVariant(QVariant::String) : QVariant(ownerName));
        q.addBindValue(email.isEmpty()        ? QVariant(QVariant::String) : QVariant(email));
        q.addBindValue(phone.isEmpty()        ? QVariant(QVariant::String) : QVariant(phone));
        q.addBindValue(address.isEmpty()      ? QVariant(QVariant::String) : QVariant(address));
        return execQuery(q, "seedUsers insertUser");
    };

    // Helper lambda to insert a compliance doc row
    auto insertDoc = [&](int userId, const QString& docType, const QString& docNumber,
                          const QString& expiryDate, const QString& provider) -> bool {
        q.prepare(
            "INSERT INTO ComplianceDocs "
            "(user_id, doc_type, doc_number, expiry_date, provider) "
            "VALUES (?, ?, ?, ?, ?);"
        );
        q.addBindValue(userId);
        q.addBindValue(docType);
        q.addBindValue(docNumber);
        q.addBindValue(expiryDate);
        q.addBindValue(provider.isEmpty() ? QVariant(QVariant::String) : QVariant(provider));
        return execQuery(q, "seedUsers insertDoc");
    };

    // ── 4 Food Vendors ────────────────────────────────────────────────────────

    // Vendor 1: Fresh Harvest Farm
    if (!insertUser(1, "freshharvest", "Fresh Harvest Farm", "Vendor", "Food",
                    "Fresh Harvest Farm", "Alice Johnson",
                    "alice@freshharvest.com", "613-555-0101",
                    "123 Farm Road, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(1, "BusinessLicence",   "BL-2025-0045", "2027-12-31", "");
    insertDoc(1, "LiabilityInsurance","POL-8821-FH",  "2027-12-31", "Intact Insurance");
    insertDoc(1, "FoodHandlerCert",   "OFH-2024-9912","2027-08-15", "");

    // Vendor 2: Sunrise Bakery
    if (!insertUser(2, "sunrisebakery", "Sunrise Bakery", "Vendor", "Food",
                    "Sunrise Bakery", "Bob Martin",
                    "bob@sunrisebakery.com", "613-555-0202",
                    "45 Baker Street, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(2, "BusinessLicence",   "BL-2025-0088", "2027-11-30", "");
    insertDoc(2, "LiabilityInsurance","POL-5544-SB",  "2027-11-30", "Desjardins Insurance");
    insertDoc(2, "FoodHandlerCert",   "OFH-2024-3301","2027-10-01", "");

    // Vendor 3: Green Valley Organics
    if (!insertUser(3, "greenvalley", "Green Valley Organics", "Vendor", "Food",
                    "Green Valley Organics", "Carol White",
                    "carol@greenvalley.com", "613-555-0303",
                    "78 Organic Lane, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(3, "BusinessLicence",   "BL-2025-0112", "2026-10-31", "");
    insertDoc(3, "LiabilityInsurance","POL-7723-GV",  "2026-10-31", "Aviva Canada");
    insertDoc(3, "FoodHandlerCert",   "OFH-2025-1104","2027-09-30", "");

    // Vendor 4: Maple Ridge Preserves (deliberately missing FoodHandlerCert — tests compliance block)
    if (!insertUser(4, "maplesyrup", "Maple Ridge Preserves", "Vendor", "Food",
                    "Maple Ridge Preserves", "David Lee",
                    "david@mapleridge.com", "613-555-0404",
                    "9 Maple Drive, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(4, "BusinessLicence",   "BL-2025-0156", "2027-12-31", "");
    insertDoc(4, "LiabilityInsurance","POL-3310-MR",  "2027-12-31", "TD Insurance");
    // ⚠ No FoodHandlerCert — intentional to test compliance blocking

    // ── 4 Artisan Vendors ─────────────────────────────────────────────────────

    // Vendor 5: Clay Creations Studio
    if (!insertUser(5, "claycreations", "Clay Creations Studio", "Vendor", "Artisan",
                    "Clay Creations Studio", "Emma Brown",
                    "emma@claycreations.com", "613-555-0505",
                    "22 Potter Street, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(5, "BusinessLicence",   "BL-2025-0201", "2027-12-31", "");
    insertDoc(5, "LiabilityInsurance","POL-9900-CC",  "2027-12-31", "Sun Life");

    // Vendor 6: WoodCraft Workshop
    if (!insertUser(6, "woodcraft", "WoodCraft Workshop", "Vendor", "Artisan",
                    "WoodCraft Workshop", "Frank Davis",
                    "frank@woodcraft.com", "613-555-0606",
                    "55 Timber Road, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(6, "BusinessLicence",   "BL-2025-0233", "2027-12-31", "");
    insertDoc(6, "LiabilityInsurance","POL-1122-WC",  "2027-12-31", "Manulife");

    // Vendor 7: Silk & Thread Textiles
    if (!insertUser(7, "silkthread", "Silk & Thread Textiles", "Vendor", "Artisan",
                    "Silk & Thread Textiles", "Grace Kim",
                    "grace@silkthread.com", "613-555-0707",
                    "33 Weaver Ave, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(7, "BusinessLicence",   "BL-2025-0277", "2027-12-31", "");
    insertDoc(7, "LiabilityInsurance","POL-4455-ST",  "2027-12-31", "Co-operators");

    // Vendor 8: The Jewelry Box
    if (!insertUser(8, "jewelrybox", "The Jewelry Box", "Vendor", "Artisan",
                    "The Jewelry Box", "Hannah Park",
                    "hannah@thejewelrybox.com", "613-555-0808",
                    "12 Gem Court, Hintonville, ON")) {
        m_db.rollback(); return false;
    }
    insertDoc(8, "BusinessLicence",   "BL-2025-0299", "2027-12-31", "");
    insertDoc(8, "LiabilityInsurance","POL-6677-JB",  "2027-12-31", "Economical Insurance");

    // ── 1 Market Operator ─────────────────────────────────────────────────────

    if (!insertUser(9, "operator", "Market Operator", "MarketOperator", "",
                    "", "", "", "", "")) {
        m_db.rollback(); return false;
    }

    // ── 1 System Administrator ────────────────────────────────────────────────

    if (!insertUser(10, "admin", "System Administrator", "SystemAdmin", "",
                    "", "", "", "", "")) {
        m_db.rollback(); return false;
    }

    m_db.commit();
    qDebug() << "[DB] Default users and compliance docs seeded.";
    return true;
}

bool DatabaseManager::seedMarketDates() {
    // Calculate 4 upcoming Sundays (same logic as the original DataStore)
    QDate today = QDate::currentDate();
    int daysUntilSunday = (7 - today.dayOfWeek()) % 7;
    QDate nextSunday = today.addDays(daysUntilSunday == 0 ? 7 : daysUntilSunday);

    m_db.transaction();
    QSqlQuery q(m_db);

    for (int i = 0; i < 4; ++i) {
        QDate marketDay = nextSunday.addDays(i * 7);
        q.prepare(
            "INSERT INTO MarketSchedule "
            "(market_id, week_number, market_date, max_food, max_artisan) "
            "VALUES (?, ?, ?, 2, 2);"
        );
        q.addBindValue(i + 1);
        q.addBindValue(i + 1);
        q.addBindValue(marketDay.toString(Qt::ISODate));  // YYYY-MM-DD
        if (!execQuery(q, "seedMarketDates")) {
            m_db.rollback();
            return false;
        }
    }

    m_db.commit();
    qDebug() << "[DB] Market schedule seeded (4 weeks, 2+2 stalls each).";
    return true;
}

// ── ORM Load: DB → Memory ─────────────────────────────────────────────────────

QList<Vendor> DatabaseManager::loadAllVendors() {
    QList<Vendor> vendors;
    QSqlQuery q(m_db);

    // Load vendor rows
    if (!q.exec("SELECT user_id, username, display_name, vendor_category, "
                "business_name, owner_name, email, phone, address "
                "FROM Users WHERE role = 'Vendor';")) {
        qCritical() << "[DB] loadAllVendors:" << q.lastError().text();
        return vendors;
    }

    while (q.next()) {
        int    id           = q.value(0).toInt();
        QString username    = q.value(1).toString();
        QString displayName = q.value(2).toString();
        QString catStr      = q.value(3).toString();
        QString businessName= q.value(4).toString();
        QString ownerName   = q.value(5).toString();
        QString email       = q.value(6).toString();
        QString phone       = q.value(7).toString();
        QString address     = q.value(8).toString();

        VendorCategory cat = (catStr == "Food")
                             ? VendorCategory::Food
                             : VendorCategory::Artisan;

        Vendor v(id, username, displayName, businessName, ownerName,
                 email, phone, address, cat);

        // ORM: load this vendor's compliance docs immediately
        QSqlQuery dq(m_db);
        dq.prepare(
            "SELECT doc_type, doc_number, expiry_date, provider "
            "FROM ComplianceDocs WHERE user_id = ?;"
        );
        dq.addBindValue(id);
        if (dq.exec()) {
            while (dq.next()) {
                QString typeStr   = dq.value(0).toString();
                QString docNum    = dq.value(1).toString();
                QDate   expiry    = QDate::fromString(dq.value(2).toString(), Qt::ISODate);
                QString provider  = dq.value(3).toString();

                DocType dtype;
                if      (typeStr == "BusinessLicence")    dtype = DocType::BusinessLicence;
                else if (typeStr == "LiabilityInsurance") dtype = DocType::LiabilityInsurance;
                else                                       dtype = DocType::FoodHandlerCert;

                v.addComplianceDoc(ComplianceDoc(dtype, docNum, expiry, provider));
            }
        }

        vendors.append(v);
    }

    return vendors;
}

QList<User> DatabaseManager::loadAllStaff() {
    QList<User> staff;
    QSqlQuery q(m_db);

    if (!q.exec("SELECT user_id, username, display_name, role "
                "FROM Users WHERE role != 'Vendor';")) {
        qCritical() << "[DB] loadAllStaff:" << q.lastError().text();
        return staff;
    }

    while (q.next()) {
        int     id          = q.value(0).toInt();
        QString username    = q.value(1).toString();
        QString displayName = q.value(2).toString();
        QString roleStr     = q.value(3).toString();

        UserType role = UserType::MarketOperator;
        if (roleStr == "SystemAdmin") role = UserType::SystemAdmin;

        staff.append(User(id, username, displayName, role));
    }

    return staff;
}

QList<MarketDate> DatabaseManager::loadAllMarketDates() {
    QList<MarketDate> dates;
    QSqlQuery q(m_db);

    if (!q.exec("SELECT market_id, market_date, max_food, max_artisan "
                "FROM MarketSchedule ORDER BY market_id ASC;")) {
        qCritical() << "[DB] loadAllMarketDates:" << q.lastError().text();
        return dates;
    }

    while (q.next()) {
        int    id        = q.value(0).toInt();
        QDate  date      = QDate::fromString(q.value(1).toString(), Qt::ISODate);
        int    maxFood   = q.value(2).toInt();
        int    maxArt    = q.value(3).toInt();

        dates.append(MarketDate(id, date, maxFood, maxArt));
    }

    // Now derive booked counts from the Bookings + Users join
    // (avoids storing redundant mutable state in MarketSchedule)
    for (MarketDate& md : dates) {
        QSqlQuery cq(m_db);

        // Count food bookings for this market date
        cq.prepare(
            "SELECT COUNT(*) FROM Bookings b "
            "JOIN Users u ON b.vendor_id = u.user_id "
            "WHERE b.market_id = ? AND u.vendor_category = 'Food';"
        );
        cq.addBindValue(md.getId());
        if (cq.exec() && cq.next()) {
            int n = cq.value(0).toInt();
            for (int i = 0; i < n; ++i) md.bookFood();
        }

        // Count artisan bookings for this market date
        cq.prepare(
            "SELECT COUNT(*) FROM Bookings b "
            "JOIN Users u ON b.vendor_id = u.user_id "
            "WHERE b.market_id = ? AND u.vendor_category = 'Artisan';"
        );
        cq.addBindValue(md.getId());
        if (cq.exec() && cq.next()) {
            int n = cq.value(0).toInt();
            for (int i = 0; i < n; ++i) md.bookArtisan();
        }
    }

    return dates;
}

QList<Booking> DatabaseManager::loadAllBookings() {
    QList<Booking> bookings;
    QSqlQuery q(m_db);

    if (!q.exec("SELECT booking_id, vendor_id, market_id, market_date "
                "FROM Bookings;")) {
        qCritical() << "[DB] loadAllBookings:" << q.lastError().text();
        return bookings;
    }

    while (q.next()) {
        int    id         = q.value(0).toInt();
        int    vendorId   = q.value(1).toInt();
        int    marketId   = q.value(2).toInt();
        QDate  date       = QDate::fromString(q.value(3).toString(), Qt::ISODate);

        bookings.append(Booking(id, vendorId, marketId, date));
    }

    return bookings;
}

QList<WaitlistEntry> DatabaseManager::loadAllWaitlistEntries() {
    QList<WaitlistEntry> entries;

    // Critical: ORDER BY entry_order ASC guarantees FIFO across all 8 queues.
    // The 8 logical queues (4 weeks × 2 categories) are partitioned at query
    // time using WHERE market_id = ? AND category = ?.
    QSqlQuery q(m_db);
    if (!q.exec(
        "SELECT entry_id, vendor_id, market_id, market_date, category, notified "
        "FROM Waitlists "
        "ORDER BY entry_order ASC;"
    )) {
        qCritical() << "[DB] loadAllWaitlistEntries:" << q.lastError().text();
        return entries;
    }

    // We need to compute FIFO positions per (market_id, category) queue
    // because WaitlistEntry::m_position is 1-based within its queue.
    QMap<QPair<int,QString>, int> positionCounter;  // key: (market_id, category)

    while (q.next()) {
        int    id        = q.value(0).toInt();
        int    vendorId  = q.value(1).toInt();
        int    marketId  = q.value(2).toInt();
        QDate  date      = QDate::fromString(q.value(3).toString(), Qt::ISODate);
        QString catStr   = q.value(4).toString();
        bool   notified  = q.value(5).toBool();

        VendorCategory cat = (catStr == "Food")
                             ? VendorCategory::Food
                             : VendorCategory::Artisan;

        WaitlistEntry entry(id, vendorId, marketId, date, cat);
        entry.setNotified(notified);

        // Assign 1-based FIFO position within this queue
        auto key = qMakePair(marketId, catStr);
        positionCounter[key]++;
        entry.setPosition(positionCounter[key]);

        entries.append(entry);
    }

    return entries;
}

// ── ORM Write: Memory → DB ────────────────────────────────────────────────────

bool DatabaseManager::insertBooking(int bookingId, int vendorId, int marketDateId,
                                     const QString& marketDateStr,
                                     const QString& confirmationNumber) {
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO Bookings "
        "(booking_id, vendor_id, market_id, market_date, confirmation_number) "
        "VALUES (?, ?, ?, ?, ?);"
    );
    q.addBindValue(bookingId);
    q.addBindValue(vendorId);
    q.addBindValue(marketDateId);
    q.addBindValue(marketDateStr);
    q.addBindValue(confirmationNumber);
    return execQuery(q, "insertBooking");
}

bool DatabaseManager::deleteBooking(int vendorId, int marketDateId) {
    QSqlQuery q(m_db);
    q.prepare(
        "DELETE FROM Bookings WHERE vendor_id = ? AND market_id = ?;"
    );
    q.addBindValue(vendorId);
    q.addBindValue(marketDateId);
    if (!execQuery(q, "deleteBooking")) return false;
    return q.numRowsAffected() > 0;
}

bool DatabaseManager::insertWaitlistEntry(int entryId, int vendorId, int marketDateId,
                                           const QString& marketDateStr,
                                           const QString& category) {
    // entry_order uses a max+1 strategy to guarantee a strictly increasing
    // FIFO sequence that survives deletions (gaps are fine — ORDER BY still works).
    QSqlQuery maxQ(m_db);
    maxQ.exec("SELECT COALESCE(MAX(entry_order), 0) FROM Waitlists;");
    int nextOrder = maxQ.next() ? maxQ.value(0).toInt() + 1 : 1;

    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO Waitlists "
        "(entry_id, entry_order, vendor_id, market_id, market_date, category, notified) "
        "VALUES (?, ?, ?, ?, ?, ?, 0);"
    );
    q.addBindValue(entryId);
    q.addBindValue(nextOrder);
    q.addBindValue(vendorId);
    q.addBindValue(marketDateId);
    q.addBindValue(marketDateStr);
    q.addBindValue(category);
    return execQuery(q, "insertWaitlistEntry");
}

bool DatabaseManager::deleteWaitlistEntry(int vendorId, int marketDateId) {
    QSqlQuery q(m_db);
    q.prepare(
        "DELETE FROM Waitlists WHERE vendor_id = ? AND market_id = ?;"
    );
    q.addBindValue(vendorId);
    q.addBindValue(marketDateId);
    if (!execQuery(q, "deleteWaitlistEntry")) return false;
    return q.numRowsAffected() > 0;
}

bool DatabaseManager::setWaitlistNotified(int vendorId, int marketDateId) {
    QSqlQuery q(m_db);
    q.prepare(
        "UPDATE Waitlists SET notified = 1 "
        "WHERE vendor_id = ? AND market_id = ?;"
    );
    q.addBindValue(vendorId);
    q.addBindValue(marketDateId);
    return execQuery(q, "setWaitlistNotified");
}

// ── Next-ID Helpers ───────────────────────────────────────────────────────────

int DatabaseManager::nextBookingId() {
    QSqlQuery q(m_db);
    q.exec("SELECT COALESCE(MAX(booking_id), 0) + 1 FROM Bookings;");
    return (q.next()) ? q.value(0).toInt() : 1;
}

int DatabaseManager::nextWaitlistId() {
    QSqlQuery q(m_db);
    q.exec("SELECT COALESCE(MAX(entry_id), 0) + 1 FROM Waitlists;");
    return (q.next()) ? q.value(0).toInt() : 1;
}

// ── Private Helpers ───────────────────────────────────────────────────────────

bool DatabaseManager::execQuery(QSqlQuery& q, const QString& context) {
    if (!q.exec()) {
        qCritical() << "[DB]" << context << "failed:" << q.lastError().text();
        return false;
    }
    return true;
}
