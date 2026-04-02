#include <QApplication>
#include <QMessageBox>
#include "ui/MainWindow.h"
#include "data/DataStore.h"
#include "data/DatabaseManager.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("HintonMarket");
    app.setApplicationDisplayName("HintonMarket — Hintonville Farmers Market");
    app.setOrganizationName("Hintonville");

    // ── Step 1: Open (or create) the SQLite database ──────────────────────────
    // DatabaseManager uses QCoreApplication::applicationDirPath() for the path,
    // so it always resolves relative to the executable — no TA path changes needed.
    if (!DatabaseManager::instance().open()) {
        QMessageBox::critical(nullptr, "Database Error",
            "HintonMarket could not open the database.\n"
            "Please ensure the application directory is writable.");
        return 1;
    }

    // ── Step 2: Load all persisted data into memory ────────────────────────────
    // DataStore::initialize() calls DatabaseManager to perform ORM load:
    // every DB row becomes a corresponding C++ object in memory.
    DataStore::instance().initialize();

    // ── Step 3: Launch the UI ──────────────────────────────────────────────────
    MainWindow window;
    window.show();

    int result = app.exec();

    // ── Step 4: Clean up DB connection on exit ────────────────────────────────
    DatabaseManager::instance().close();

    return result;
}
