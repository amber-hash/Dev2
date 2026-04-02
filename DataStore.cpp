#include "DataStore.h"
#include "DatabaseManager.h"
#include <QDate>

// ── Singleton ─────────────────────────────────────────────────────────────────

DataStore& DataStore::instance() {
    static DataStore store;
    return store;
}

// ── Initialize: load everything from DB into memory ───────────────────────────

void DataStore::initialize() {
    // Clear all in-memory collections before (re)loading
    m_vendors.clear();
    m_staff.clear();
    m_marketDates.clear();
    m_bookings.clear();
    m_waitlist.clear();

    DatabaseManager& db = DatabaseManager::instance();

    // ORM load: each call queries the DB and constructs fully-populated objects
    m_vendors      = db.loadAllVendors();
    m_staff        = db.loadAllStaff();
    m_marketDates  = db.loadAllMarketDates();
    m_bookings     = db.loadAllBookings();
    m_waitlist     = db.loadAllWaitlistEntries();

    // Sync next-ID counters from the DB so generated IDs never collide
    m_nextBookingId  = db.nextBookingId();
    m_nextWaitlistId = db.nextWaitlistId();
}

// ── User lookup ───────────────────────────────────────────────────────────────

User* DataStore::findUser(const QString& username) {
    for (auto& v : m_vendors) {
        if (v.getUsername().toLower()    == username.toLower() ||
            v.getDisplayName().toLower() == username.toLower())
            return &v;
    }
    for (auto& u : m_staff) {
        if (u.getUsername().toLower()    == username.toLower() ||
            u.getDisplayName().toLower() == username.toLower())
            return &u;
    }
    return nullptr;
}

Vendor* DataStore::findVendor(const QString& username) {
    for (auto& v : m_vendors) {
        if (v.getUsername().toLower()    == username.toLower() ||
            v.getDisplayName().toLower() == username.toLower())
            return &v;
    }
    return nullptr;
}

Vendor* DataStore::findVendorById(int id) {
    for (auto& v : m_vendors) {
        if (v.getId() == id) return &v;
    }
    return nullptr;
}

// ── Market schedule ───────────────────────────────────────────────────────────

MarketDate* DataStore::findMarketDate(int id) {
    for (auto& md : m_marketDates) {
        if (md.getId() == id) return &md;
    }
    return nullptr;
}

// ── Bookings ──────────────────────────────────────────────────────────────────

QList<Booking> DataStore::getBookingsForVendor(int vendorId) const {
    QList<Booking> result;
    for (const auto& b : m_bookings) {
        if (b.getVendorId() == vendorId) result.append(b);
    }
    return result;
}

bool DataStore::vendorHasBooking(int vendorId, int marketDateId) const {
    for (const auto& b : m_bookings) {
        if (b.getVendorId() == vendorId && b.getMarketDateId() == marketDateId)
            return true;
    }
    return false;
}

bool DataStore::bookStall(int vendorId, int marketDateId) {
    Vendor*     vendor = findVendorById(vendorId);
    MarketDate* md     = findMarketDate(marketDateId);
    if (!vendor || !md) return false;

    // Compliance gate
    if (!vendor->hasAllRequiredDocs()) return false;

    // One active booking at a time
    if (!getBookingsForVendor(vendorId).isEmpty()) return false;

    // No duplicate booking for the same date
    if (vendorHasBooking(vendorId, marketDateId)) return false;

    // Stall availability
    bool available = (vendor->getCategory() == VendorCategory::Food)
                     ? md->hasFoodAvailability()
                     : md->hasArtisanAvailability();
    if (!available) return false;

    // ── Persist FIRST (fail fast if DB is unavailable) ────────────────────────
    int     newId   = m_nextBookingId;
    QString confNum = QString("HM-%1-%2")
                      .arg(md->getDate().toString("yyyyMMdd"))
                      .arg(newId);

    if (!DatabaseManager::instance().insertBooking(
            newId, vendorId, marketDateId,
            md->getDate().toString(Qt::ISODate), confNum)) {
        return false;  // DB write failed — do not update memory
    }

    // ── Update in-memory state ────────────────────────────────────────────────
    Booking b(m_nextBookingId++, vendorId, marketDateId, md->getDate());
    m_bookings.append(b);

    if (vendor->getCategory() == VendorCategory::Food) md->bookFood();
    else                                                 md->bookArtisan();

    leaveWaitlist(vendorId, marketDateId);  // auto-remove from waitlist if present

    vendor->addNotification(
        QString("✅ Booking confirmed for %1 (Conf: %2)")
        .arg(md->getDate().toString("MMMM d, yyyy"))
        .arg(b.getConfirmationNumber()));

    return true;
}

bool DataStore::bookStallByOperator(int vendorId, int marketDateId) {
    Vendor*     vendor = findVendorById(vendorId);
    MarketDate* md     = findMarketDate(marketDateId);
    if (!vendor || !md) return false;

    // One active booking at a time — still enforced
    if (!getBookingsForVendor(vendorId).isEmpty()) return false;

    // No duplicate booking for the same date
    if (vendorHasBooking(vendorId, marketDateId)) return false;

    // Stall availability — still enforced
    bool available = (vendor->getCategory() == VendorCategory::Food)
                     ? md->hasFoodAvailability()
                     : md->hasArtisanAvailability();
    if (!available) return false;

    // Persist first
    int     newId   = m_nextBookingId;
    QString confNum = QString("HM-%1-%2")
                      .arg(md->getDate().toString("yyyyMMdd"))
                      .arg(newId);

    if (!DatabaseManager::instance().insertBooking(
            newId, vendorId, marketDateId,
            md->getDate().toString(Qt::ISODate), confNum)) {
        return false;
    }

    // Update memory
    Booking b(m_nextBookingId++, vendorId, marketDateId, md->getDate());
    m_bookings.append(b);

    if (vendor->getCategory() == VendorCategory::Food) md->bookFood();
    else                                                 md->bookArtisan();

    leaveWaitlist(vendorId, marketDateId);

    vendor->addNotification(
        QString("✅ Stall booked by Market Operator for %1 (Conf: %2)")
        .arg(md->getDate().toString("MMMM d, yyyy"))
        .arg(b.getConfirmationNumber()));

    return true;
}

bool DataStore::cancelBooking(int vendorId, int marketDateId) {
    Vendor*     vendor = findVendorById(vendorId);
    MarketDate* md     = findMarketDate(marketDateId);
    if (!vendor || !md) return false;

    for (int i = 0; i < m_bookings.size(); ++i) {
        if (m_bookings[i].getVendorId()    == vendorId &&
            m_bookings[i].getMarketDateId() == marketDateId) {

            // ── Persist FIRST ─────────────────────────────────────────────────
            if (!DatabaseManager::instance().deleteBooking(vendorId, marketDateId))
                return false;

            // ── Update in-memory state ────────────────────────────────────────
            m_bookings.removeAt(i);

            if (vendor->getCategory() == VendorCategory::Food) md->cancelFood();
            else                                                  md->cancelArtisan();

            vendor->addNotification(
                QString("❌ Booking cancelled for %1")
                .arg(md->getDate().toString("MMMM d, yyyy")));

            processWaitlistOnCancellation(marketDateId, vendor->getCategory());
            return true;
        }
    }
    return false;
}

// ── Waitlist ──────────────────────────────────────────────────────────────────

QList<WaitlistEntry> DataStore::getWaitlistForVendor(int vendorId) const {
    QList<WaitlistEntry> result;
    for (const auto& w : m_waitlist) {
        if (w.getVendorId() == vendorId) result.append(w);
    }
    return result;
}

bool DataStore::vendorOnWaitlist(int vendorId, int marketDateId) const {
    for (const auto& w : m_waitlist) {
        if (w.getVendorId() == vendorId && w.getMarketDateId() == marketDateId)
            return true;
    }
    return false;
}

bool DataStore::vendorWaitlistNotified(int vendorId, int marketDateId) const {
    for (const auto& w : m_waitlist) {
        if (w.getVendorId() == vendorId && w.getMarketDateId() == marketDateId)
            return w.isNotified();
    }
    return false;
}

int DataStore::getWaitlistPosition(int vendorId, int marketDateId) const {
    // Find the vendor's category so we can partition the queue correctly.
    // The 8 logical queues are: (market_id=1..4) × (category=Food|Artisan).
    Vendor* vendor = const_cast<DataStore*>(this)->findVendorById(vendorId);
    if (!vendor) return -1;

    int pos = 1;
    for (const auto& w : m_waitlist) {
        if (w.getMarketDateId() == marketDateId &&
            w.getCategory()     == vendor->getCategory()) {
            if (w.getVendorId() == vendorId) return pos;
            ++pos;
        }
    }
    return -1;
}

bool DataStore::joinWaitlist(int vendorId, int marketDateId) {
    if (vendorOnWaitlist(vendorId, marketDateId)) return false;

    Vendor*     vendor = findVendorById(vendorId);
    MarketDate* md     = findMarketDate(marketDateId);
    if (!vendor || !md)              return false;
    if (!vendor->hasAllRequiredDocs()) return false;

    // Count current length of this specific queue (market + category)
    int queueLen = 0;
    for (const auto& w : m_waitlist) {
        if (w.getMarketDateId() == marketDateId &&
            w.getCategory()     == vendor->getCategory())
            ++queueLen;
    }

    // ── Persist FIRST ─────────────────────────────────────────────────────────
    int     newId   = m_nextWaitlistId;
    QString catStr  = (vendor->getCategory() == VendorCategory::Food) ? "Food" : "Artisan";

    if (!DatabaseManager::instance().insertWaitlistEntry(
            newId, vendorId, marketDateId,
            md->getDate().toString(Qt::ISODate), catStr)) {
        return false;
    }

    // ── Update in-memory state ────────────────────────────────────────────────
    WaitlistEntry w(m_nextWaitlistId++, vendorId, marketDateId,
                    md->getDate(), vendor->getCategory());
    w.setPosition(queueLen + 1);
    m_waitlist.append(w);

    vendor->addNotification(
        QString("⏳ Added to waitlist for %1 (Position: %2)")
        .arg(md->getDate().toString("MMMM d, yyyy"))
        .arg(queueLen + 1));

    return true;
}

bool DataStore::leaveWaitlist(int vendorId, int marketDateId) {
    Vendor*     vendor = findVendorById(vendorId);
    MarketDate* md     = findMarketDate(marketDateId);
    if (!vendor || !md) return false;

    for (int i = 0; i < m_waitlist.size(); ++i) {
        if (m_waitlist[i].getVendorId()    == vendorId &&
            m_waitlist[i].getMarketDateId() == marketDateId) {

            VendorCategory cat = m_waitlist[i].getCategory();

            // ── Persist FIRST ─────────────────────────────────────────────────
            if (!DatabaseManager::instance().deleteWaitlistEntry(vendorId, marketDateId))
                return false;

            // ── Update in-memory state ────────────────────────────────────────
            m_waitlist.removeAt(i);

            // Recompute 1-based FIFO positions for this queue after removal
            int pos = 1;
            for (auto& w : m_waitlist) {
                if (w.getMarketDateId() == marketDateId && w.getCategory() == cat)
                    w.setPosition(pos++);
            }

            vendor->addNotification(
                QString("🚫 Removed from waitlist for %1")
                .arg(md->getDate().toString("MMMM d, yyyy")));

            return true;
        }
    }
    return false;
}

void DataStore::processWaitlistOnCancellation(int marketDateId, VendorCategory category) {
    // Find the #1 vendor in the queue for this specific (market_id, category) pair.
    // m_waitlist is loaded in entry_order ASC, so the first matching entry IS
    // the FIFO head of that queue.
    for (auto& w : m_waitlist) {
        if (w.getMarketDateId() == marketDateId &&
            w.getCategory()     == category     &&
            w.getPosition()     == 1) {

            Vendor*     vendor = findVendorById(w.getVendorId());
            MarketDate* md     = findMarketDate(marketDateId);
            if (vendor && md) {
                w.setNotified(true);

                // Persist the notification flag
                DatabaseManager::instance().setWaitlistNotified(
                    w.getVendorId(), marketDateId);

                vendor->addNotification(
                    QString("🔔 A stall is now available for %1! "
                            "You are #1 on the waitlist. Please book your stall.")
                    .arg(md->getDate().toString("MMMM d, yyyy")));
            }
            return;
        }
    }
}
