#include "tais.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <stdexcept>

// Inspect a playback interval without launching the radar or printing JMS bodies.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    const auto start=QDateTime::fromString(args.value(2),Qt::ISODate);
    if(args.size()!=3 || !start.isValid() || !args[2].endsWith('Z')) {
        qCritical()<<"Usage: taischeck FACILITY YYYY-MM-DDTHH:MM:SSZ";
        return 2;
    }
    stars::net::TaisCache cache(args[1]);
    if(!cache.configured()) {
        qCritical()<<"Set STARS_TAIS_DIR or TAIS_BUCKET";
        return 2;
    }
    const auto second=start.toSecsSinceEpoch();
    QTimer poll;
    QObject::connect(&poll,&QTimer::timeout,&app,[&] {
        try { cache.prepare(second); }
        catch(const std::out_of_range& error) { qCritical()<<error.what(); app.exit(2); return; }
        if(!cache.error().isEmpty()) { app.exit(1); return; }
        if(!cache.ready()) return;
        const auto rows=cache.between(second*1000000000,(second+cache.lookaheadSeconds)*1000000000);
        if(!rows) { app.exit(1); return; }
        qInfo()<<rows->size()<<"deliveries in"<<cache.lookaheadSeconds<<"seconds";
        app.quit();
    });
    poll.start(50);
    QTimer::singleShot(60000,&app,[&] { qCritical()<<"Archive load timed out"; app.exit(1); });
    return app.exec();
}
