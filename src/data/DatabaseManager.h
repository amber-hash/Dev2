#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QString>
#include <QList>
#include <QDebug>

#include "../models/User.h"
#include "../models/Vendor.h"
#include "../models/MarketDate.h"
#include "../models/Booking.h"
#include "../models/WaitlistEntry.h"
#include "../models/ComplianceDoc.h"

/**
 * DatabaseManager — SQLite persistence layer for HintonMarket
 *
 * This class is a singleton responsible for ALL database interactions.
 * It follows the DAO (Data Access Object) pattern: each public method
 * maps cleanly to one or more SQL operations and returns fully-constructed
 * C++ objects in memory (ORM).
 *
 * Architecture:
 *   UI Layer  →  MarketManager (Controller)  →  DataStore (in-memory)
 *                                            →  DatabaseManager (SQLite)
 *
 * On startup:
 *   - If the .sqlite3 file does NOT exist → create schema + seed default data.
 *   - If the .sqlite3 file DOES exist     → open it and load all data into
 *     DataStore memory structures.
 *
 * Path: QCoreApplication::applicationDirPath() + "/hintonMarket.sqlite3"
 * This ensures the TA can open the project from any directory.
 */
class DatabaseManager {
public:
    // Singleton accessor
    static DatabaseManager& instance();

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * Opens (or creates) the database.
     * Returns true on success, false on any fatal error.
     * Call this ONCE from main() before DataStore::initialize().
     */
    bool open();

    /** Close the database connection (called on shutdown). */
    void close();

    /** True if a valid connection is open. */
    bool isOpen() const;

    // ── Schema & Seeding ─────────────────────────────────────────────────────

    /**
     * Creates all tables (if they don't exist) and inserts the required
     * default data (10 users, 4 market weeks). Only called when the DB
     * file is created for the first time.
     */
    bool initializeSchema();

    // ── ORM Load (DB → Memory) ───────────────────────────────────────────────

    /** Load all Vendor rows and their ComplianceDocs into a QList. */
    QList<Vendor> loadAllVendors();

    /** Load all non-vendor User rows (operators, admins) into a QList. */
    QList<User> loadAllStaff();

    /** Load all MarketDate rows (with booked counts derived from Bookings). */
    QList<MarketDate> loadAllMarketDates();

    /** Load all Booking rows. */
    QList<Booking> loadAllBookings();

    /** Load all WaitlistEntry rows, ordered by FIFO (entry_order ASC). */
    QList<WaitlistEntry> loadAllWaitlistEntries();

    // ── ORM Write (Memory → DB) ──────────────────────────────────────────────

    /**
     * Insert a new booking into the Bookings table.
     * Returns true on success.
     */
    bool insertBooking(int bookingId, int vendorId, int marketDateId,
                       const QString& marketDateStr,
                       const QString& confirmationNumber);

    /**
     * Delete a booking by vendorId + marketDateId.
     * Returns true if a row was deleted.
     */
    bool deleteBooking(int vendorId, int marketDateId);

    /**
     * Insert a new waitlist entry.
     * entry_order is auto-assigned by the DB (AUTOINCREMENT) to guarantee FIFO.
     * Returns true on success.
     */
    bool insertWaitlistEntry(int entryId, int vendorId, int marketDateId,
                              const QString& marketDateStr,
                              const QString& category);

    /**
     * Delete a waitlist entry by vendorId + marketDateId.
     * Returns true if a row was deleted.
     */
    bool deleteWaitlistEntry(int vendorId, int marketDateId);

    /**
     * Mark the first-in-queue waitlist entry for a given market date and
     * category as notified (notified = 1).
     */
    bool setWaitlistNotified(int vendorId, int marketDateId);

    // ── Next-ID Helpers ──────────────────────────────────────────────────────

    /** Returns max(booking_id) + 1 from Bookings, or 1 if empty. */
    int nextBookingId();

    /** Returns max(entry_id) + 1 from Waitlists, or 1 if empty. */
    int nextWaitlistId();

private:
    DatabaseManager() = default;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // ── Private Helpers ──────────────────────────────────────────────────────
    bool createTables();
    bool seedUsers();
    bool seedMarketDates();

    bool execQuery(QSqlQuery& q, const QString& context);

    QSqlDatabase m_db;
};
