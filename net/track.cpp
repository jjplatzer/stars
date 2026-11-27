#include "track.h"
#include "tais.h"
#include <QXmlStreamReader>
#include <QHash>
#include <QtConcurrentRun>
#include <cmath>

namespace stars::net {
namespace {
using Fields=QHash<QString,QString>;
Fields fields(QXmlStreamReader& xml) {
    Fields result;
    while(xml.readNextStartElement()) {
        const auto key=xml.name().toString();
        result.insert(key,xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed());
    }
    return result;
}
bool flag(const QString& s) { return s=="1" || s.compare("true",Qt::CaseInsensitive)==0; }
QString planKey(const std::optional<FlightPlan>& plan) {
    if(!plan) return {};
    if(!plan->sfpn.isEmpty()) return "F"+plan->sfpn;
    return plan->gufi.isEmpty()?QString{}:"G"+plan->gufi;
}
FlightPlan flightPlan(const Fields& f) {
    FlightPlan p;
    p.sfpn=f.value("sfpn"); p.acid=f.value("acid").toUpper(); p.cps=f.value("cps").toUpper();
    p.ocr=f.value("ocr").toUpper().replace('_',' '); p.direction=f.value("lld").toUpper();
    p.assignedBeacon=f.value("assignedBeaconCode"); p.primary=f.value("scratchPad1").toUpper();
    p.secondary=f.value("scratchPad2").toUpper(); p.aircraftType=f.value("acType").toUpper();
    p.category=f.value("category").toUpper(); p.rules=f.value("rawFlightRules").toUpper();
    p.runway=f.value("runway").toUpper();
    p.flightRules=f.value("flightRules").toUpper().replace('_',' ').replace('-',' ');
    const auto& rules=p.flightRules;
    p.ifr=p.rules=="I" || rules=="I" || rules=="IFR" || rules=="VFR ON TOP";
    p.vfr=p.rules=="V" || rules=="V" || rules=="VFR";
    if(p.rules.size()!=1) {
        p.rules=f.value("flightRules").toUpper();
        if(p.rules=="VFR") p.rules="V";
        if(p.rules!="V" && p.rules!="P" && p.rules!="E") p.rules=" ";
    }
    p.equipmentSuffix=f.value("eqptSuffix").toUpper();
    p.entryFix=f.value("entryFix").toUpper(); p.airport=f.value("airport"); p.exitFix=f.value("exitFix").toUpper(); p.type=f.value("type").toUpper();
    p.assignedAltitude=f.value("assignedAltitude").toInt(); p.requestedAltitude=f.value("requestedAltitude").toInt();
    p.rnav=f.value("rnav").toInt()!=0; p.suspended=flag(f.value("suspended"));
    p.deleted=flag(f.value("delete")) || f.value("status").compare("terminated",Qt::CaseInsensitive)==0;
    return p;
}
}
QList<TrackReport> parseTrackReports(const QByteArray& body,const QString& facility) {
    QXmlStreamReader xml(body);
    QList<TrackReport> reports;
    if(!xml.readNextStartElement() || xml.name()!=u"TATrackAndFlightPlan") return {};
    QString source;
    while(xml.readNextStartElement()) {
        if(xml.name()==u"src" || xml.name()==u"srcTracon" || xml.name()==u"tracon") {
            auto id=xml.readElementText().trimmed();
            if(source.isEmpty() || xml.name()==u"src") source=id;
        } else if(xml.name()==u"record") {
            TrackReport r;
            Fields enhanced;
            while(xml.readNextStartElement()) {
                if(xml.name()==u"recSAFAReceiptTime") {
                    const auto stamp=QDateTime::fromString(xml.readElementText(),Qt::ISODateWithMs);
                    if(stamp.isValid()) r.sourceMs=stamp.toMSecsSinceEpoch();
                } else if(xml.name()==u"flightPlan") r.plan=flightPlan(fields(xml));
                else if(xml.name()==u"enhancedData") enhanced=fields(xml);
                else if(xml.name()==u"track") {
                    const auto t=fields(xml);
                    r.number=t.value("trackNum");
                    r.pseudo=flag(t.value("pseudo")) || r.number=="0";
                    if(r.pseudo) r.number.clear();
                    r.address=t.value("acAddress"); r.status=t.value("status").toLower();
                    r.beacon=t.value("reportedBeaconCode"); r.newTrack=flag(t.value("new"));
                    bool vxOk=false,vyOk=false;
                    const double vx=t.value("vx").toDouble(&vxOk),vy=t.value("vy").toDouble(&vyOk);
                    r.speed=std::hypot(vx,vy);
                    if(vxOk && vyOk && std::isfinite(vx) && std::isfinite(vy)) r.velocity=QPointF(vx,vy);
                    r.frozen=flag(t.value("frozen"));
                    bool ok=false,lonOk=false;
                    const auto alt=t.value("reportedAltitude").toInt(&ok);
                    if(ok) r.altitude=alt;
                    const auto lat=t.value("lat").toDouble(&ok),lon=t.value("lon").toDouble(&lonOk);
                    if(ok && lonOk && std::abs(lon)<=180 && std::abs(lat)<=90 && (lat!=0 || lon!=0))
                        r.location=QPointF(lon,lat);
                    const auto mrt=QDateTime::fromString(t.value("mrtTime"),Qt::ISODateWithMs);
                    if(mrt.isValid()) r.mrtMs=mrt.toMSecsSinceEpoch();
                } else xml.skipCurrentElement();
            }
            if(r.plan) {
                r.plan->origin=enhanced.value("departureAirport").toUpper();
                r.plan->destination=enhanced.value("destinationAirport").toUpper();
                r.plan->gufi=enhanced.value("sfdpsGufi",enhanced.value("eramGufi"));
            }
            if(!r.number.isEmpty() || r.plan) reports.append(std::move(r));
        } else xml.skipCurrentElement();
    }
    while(!xml.atEnd()) xml.readNext();
    if(xml.hasError() || source.compare(facility,Qt::CaseInsensitive)!=0) return {};
    return reports;
}

Traffic::~Traffic() { if(pending_) pending_->work.cancel(); }
void Traffic::reset() {
    if(pending_) pending_->work.cancel();
    pending_.reset();
    tracks_.clear();
    dropped_.clear();
    plans_.clear(); deletedPlans_.clear(); trackPlans_.clear();
    cursor_.reset();
    lastReceipt_.reset();
}
bool Traffic::acknowledgeHandoff(const QString& number,const QString& tcp) {
    auto it=tracks_.find(number);
    if(tcp.isEmpty() || it==tracks_.end() || it->formerOwner!=tcp) return false;
    it->formerOwner.clear();
    if(auto plan=plans_.find(trackPlans_.value(number));plan!=plans_.end()) plan->formerOwner.clear();
    return true;
}

void Traffic::updatePlan(const TrackReport& r,qint64 receipt) {
    const auto& fp=r.plan;
    const auto key=planKey(fp);
    const auto source=r.sourceMs?r.sourceMs:receipt;
    auto current=plans_.find(key);
    const auto detachRadar=[&](const QString& number) {
        trackPlans_.remove(number);
        if(auto old=tracks_.find(number);old!=tracks_.end()) {
            old->report.plan.reset(); old->owner.clear(); old->formerOwner.clear();
            old->pending=false; old->associationSinceMs=receipt;
        }
    };
    if(!key.isEmpty() && ((current!=plans_.end() && current->sourceMs>source) || deletedPlans_.value(key)>=source)) return;
    if(!r.number.isEmpty()) {
        const auto previous=trackPlans_.value(r.number);
        if(!previous.isEmpty() && (previous!=key || r.status=="drop" || (fp && fp->deleted))) {
            if(auto old=plans_.find(previous);old!=plans_.end()) {
                old->track.clear(); old->pending=false; old->lastMrt=0; old->altitude.reset();
            }
            trackPlans_.remove(r.number);
        }
    }
    if(key.isEmpty()) return;
    if(fp->deleted) {
        deletedPlans_[key]=source;
        if(current!=plans_.end()) { detachRadar(current->track); plans_.erase(current); }
        return;
    }
    deletedPlans_.remove(key);
    if(current!=plans_.end() && fp->acid!=current->plan.acid && !fp->gufi.isEmpty() &&
       !current->plan.gufi.isEmpty() && fp->gufi!=current->plan.gufi) {
        detachRadar(current->track);
        plans_.erase(current);
    }
    auto& p=plans_[key];
    p.sourceMs=source;
    if(!p.firstSeen) p.firstSeen=receipt;
    if(fp->vfr && !p.plan.vfr) p.vfrSince=receipt;
    if(fp->suspended && !p.plan.suspended) p.suspendSince=receipt;
    const auto prior=p.plan;
    p.plan=*fp;
    if(p.plan.origin.isEmpty()) p.plan.origin=prior.origin;
    if(p.plan.destination.isEmpty()) p.plan.destination=prior.destination;
    if(p.plan.gufi.isEmpty()) p.plan.gufi=prior.gufi;
    if(r.number.isEmpty()) {
        if(r.pseudo && p.track.isEmpty()) { p.pending=true; p.altitude=r.altitude; }
        if(prior.suspended && !fp->suspended && p.track.isEmpty()) p.pending=false;
    } else {
        p.pending=false;
        if(r.status=="drop") return;
        if(!p.track.isEmpty() && p.track!=r.number) detachRadar(p.track);
        p.track=r.number; trackPlans_[r.number]=key;
        p.altitude=r.altitude; p.beacon=r.beacon; p.lastMrt=r.mrtMs;
        p.frozen=r.frozen; p.coasting=r.status.startsWith("coast");
    }
}
void Traffic::apply(const TrackReport& r,qint64 receipt,bool history) {
    QString key=r.number;
    if(key.isEmpty()) {
        updatePlan(r,receipt);
        const auto listed=plans_.constFind(planKey(r.plan));
        if(listed==plans_.cend() || !tracks_.contains(listed->track)) return;
        key=listed->track;
    }
    auto it=tracks_.find(key);
    const bool wasAssociated=it!=tracks_.end() && it->report.plan.has_value();
    if(!r.number.isEmpty()) {
        if(r.mrtMs<=0 || r.mrtMs<=dropped_.value(key) || (it!=tracks_.end() && r.mrtMs<it->report.mrtMs)) return;
        if(r.status=="drop") {
            updatePlan(r,receipt);
            dropped_[key]=r.mrtMs;
            tracks_.remove(key);
            return;
        }
        if(!r.location || r.mrtMs+coastSeconds*1000<receipt) return;
        updatePlan(r,receipt);
        auto& t=tracks_[key];
        if(r.mrtMs>t.report.mrtMs && (r.newTrack || receipt-t.report.mrtMs>coastSeconds*1000 || (!r.address.isEmpty() && !t.report.address.isEmpty() && r.address!=t.report.address))) t={};
        if(history && t.report.location && r.mrtMs>t.report.mrtMs) {
            t.history.append({t.report.mrtMs,*t.report.location,t.report.altitude});
            while(!t.history.isEmpty() && (t.history.size()>64 || t.history.first().time<r.mrtMs-60000)) t.history.removeFirst();
        }
        if(!t.firstMrtMs) t.firstMrtMs=r.mrtMs;
        if(t.report.beacon!=r.beacon) t.beaconSinceMs=receipt;
        t.report=r;
        it=tracks_.find(key);
    }
    auto& target=*it;
    // Independent plan updates and the radar block must use the same revision.
    // The TAIS source timestamp can lag a newer plan delivered earlier.
    const auto linked=plans_.constFind(trackPlans_.value(key));
    if(linked!=plans_.cend()) target.report.plan=linked->plan;
    else if(planKey(r.plan).isEmpty() && r.plan && !r.plan->deleted) target.report.plan=r.plan;
    else target.report.plan.reset();
    const auto& plan=target.report.plan;
    if(wasAssociated!=plan.has_value()) target.associationSinceMs=receipt;
    if(!plan) { target.owner.clear(); target.formerOwner.clear(); }
    else if(plan->ocr!="PENDING") {
        if(target.pending && !target.owner.isEmpty() && target.owner!=plan->cps) {
            target.formerOwner=target.owner;
            target.acceptedMs=receipt;
        }
        if(!target.pending && target.owner!=plan->cps) target.formerOwner.clear();
        target.owner=plan->cps;
    }
    target.pending=target.report.plan && target.report.plan->ocr=="PENDING";
    if(auto plan=plans_.find(trackPlans_.value(key));plan!=plans_.end()) {
        plan->owner=target.owner; plan->formerOwner=target.formerOwner;
    }
}

bool Traffic::advance(const TaisCache& cache,qint64 ms) {
    const qint64 through=ms*1000000+1;
    bool changed=false;
    const auto latest=pending_ ? std::optional(pending_->through) : cursor_;
    if((latest && (through<*latest || through-*latest>coastSeconds*1000000000LL)) || revision_!=cache.revision()) {
        reset();
        changed=true;
    }
    revision_=cache.revision();
    if(pending_) {
        if(!pending_->work.isFinished()) return changed;
        for(const auto& batch:pending_->work.takeResult()) {
            for(const auto& report:batch.reports) {
                if(!report.number.isEmpty()) lastReceipt_=batch.receivedMs;
                apply(report,batch.receivedMs,!pending_->seed);
            }
        }
        cursor_=pending_->through;
        pending_.reset();
        changed=true;
    }
    if(!changed && cursor_ && through==*cursor_) return false;
    const auto expired=tracks_.removeIf([ms](auto it){ return it.value().report.mrtMs+coastSeconds*1000<=ms; });
    dropped_.removeIf([ms](auto it){ return it.value()+coastSeconds*1000<=ms; });
    changed=changed || expired;
    if(cursor_ && through==*cursor_) return changed;
    const auto from=cursor_.value_or(std::max<qint64>(0,through-1-ownershipLookbehindSeconds*1000000000LL));
    auto deliveries=cache.between(from,through);
    if(!deliveries) return changed;
    if(deliveries->isEmpty()) { cursor_=through; return changed; }
    // A cold start also replays earlier ownership changes. Pending reports name
    // the receiver, while the last accepted report identifies the current owner.
    pending_.emplace(Pending{through,!cursor_,QtConcurrent::run([rows=std::move(*deliveries)](QPromise<QList<Batch>>& promise) {
        QList<Batch> batches;
        for(const auto& d:rows) {
            if(promise.isCanceled()) return;
            if(!d.root.isEmpty() && d.root!="TATrackAndFlightPlan") continue;
            auto reports=parseTrackReports(d.body,d.facility);
            if(!reports.isEmpty()) batches.append({d.receivedNs/1000000,std::move(reports)});
        }
        promise.addResult(std::move(batches));
    })});
    return changed;
}
}
