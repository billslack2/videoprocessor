#pragma once
#include "../VideoProcessor-ChartCore/ChartData.h"
#include <QDataStream>
#include <QThreadPool>
#include <QThread>
#include <QRunnable>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>

// Immutable snapshots cross the worker boundary. No QWidget or editor callback
// is ever accessed there. One low-priority worker serializes library sampling.
namespace ToneChartWork {
using Request=ToneCurveProvider::Request;
struct Work {
    Request request;
    std::vector<std::tuple<QString,QColor,Request>> others;
    QString name;
    QColor color;
    bool showDefault=true;
    int display=1,zone=1;
    size_t patch=13;
};
struct Result { ToneCurveChart::Data tone; ColourChart::Data map,sweep; QString error; bool colour=false; };
struct Job { std::atomic_bool done{false}; Result result; quint64 generation=0; };
inline QByteArray Key(const Request& r) {
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);
    const auto& f=ToneCurveProvider::Registered();
    out<<quint64(reinterpret_cast<quintptr>(f.sample))<<quint64(reinterpret_cast<quintptr>(f.colour));
    out<<QString::fromStdString(r.quality)<<QString::fromStdString(r.toneMapping)
       <<QString::fromStdString(r.peakDetection)<<QString::fromStdString(r.gamutMapping);
    out<<r.dynamicTarget<<r.gamutOverflowCorrection<<r.betaRenderer<<r.defaultsKnown
       <<r.targetNits<<r.blackNits<<r.scenePeakNits<<r.sceneAverageKnown<<r.masteringPeakNits;
    for(const auto& v:r.toneTuning) out<<v.specified<<v.number;
    out<<quint64(r.sceneMix.size());for(const auto& v:r.sceneMix) out<<v.share<<v.nits;
    out<<quint64(r.sourcePq.size());for(double v:r.sourcePq) out<<v;
    return bytes;
}
// Include presentation and comparison inputs, so repeated editor refreshes
// reuse the outstanding request without suppressing an actual chart edit.
inline QByteArray Key(const Work& work) {
    QByteArray bytes;QDataStream out(&bytes,QIODevice::WriteOnly);
    out<<Key(work.request)<<work.name<<work.color<<work.showDefault
       <<work.display<<work.zone<<quint64(work.patch)<<quint64(work.others.size());
    for(const auto& [name,color,request]:work.others)out<<name<<color<<Key(request);
    return bytes;
}

template<class T> struct Cache {
    std::map<QByteArray,T> values; std::deque<QByteArray> order;
    const T* find(const QByteArray& key) {
        auto it=values.find(key);if(it==values.end())return nullptr;
        order.erase(std::remove(order.begin(),order.end(),key),order.end());order.push_back(key);
        return &it->second;
    }
    void put(const QByteArray& key,const T& value) {
        order.erase(std::remove(order.begin(),order.end(),key),order.end());
        if(values.size()>=128&&values.find(key)==values.end()){values.erase(order.front());order.pop_front();}
        values[key]=value;order.push_back(key);
    }
};
class Service {
    struct Colour { ColourChart::Data map,sweep; };
    Cache<ToneCurveChart::Data> tones_;
    Cache<Colour> colours_;
    std::mutex warmMutex_;
    std::set<QByteArray> warming_;
    // Destroy/join the pool before its caches and mutex.
    QThreadPool pool_;
    bool tone(const Request& request,ToneCurveChart::Data& data,QString& error) {
        const auto key=Key(request);
        if(const auto* hit=tones_.find(key)){data=*hit;return true;}
        std::wstring why;
        if(!ChartData::Tone(request,data,why)){error=QString::fromStdWString(why);return false;}
        tones_.put(key,data);return true;
    }
    bool colour(const Request& request,double nits,const Work& work,Colour& data) {
        QByteArray key=Key(request);QDataStream out(&key,QIODevice::Append);
        out<<nits<<work.display<<work.zone<<quint64(work.patch);
        if(const auto* hit=colours_.find(key)){data=*hit;return true;}
        if(!ChartData::Colour(request,nits,work.display,work.zone,work.patch,
            QStringLiteral("Profile"),QColor("#49b6ff"),true,{},data.map,data.sweep))return false;
        colours_.put(key,data);return true;
    }
    Result compute(const Work& work) {
        Result result;
        if(!tone(work.request,result.tone,result.error)){
            if(result.error.isEmpty())result.error=QStringLiteral("Charts need VP Renderer's files.");
            return result;
        }
        result.tone.profileName=work.name;result.tone.profileColor=work.color;
        result.tone.showDefault=work.showDefault;
        const double nits=ChartData::ZoneNits(work.request,result.tone.result,work.zone);
        Colour primary;
        result.colour=colour(work.request,nits,work,primary);
        if(result.colour){
            result.map=std::move(primary.map);result.sweep=std::move(primary.sweep);
            for(auto* chart:{&result.map,&result.sweep}){
                if(!chart->series.empty()) {chart->series[0].name=work.name;chart->series[0].color=work.color;}
                if(!work.showDefault&&chart->series.size()>1)chart->series.erase(chart->series.begin()+1);
            }
        }
        for(const auto& [name,color,request]:work.others){
            ToneCurveChart::Data other;QString ignored;
            if(tone(request,other,ignored))result.tone.compares.push_back({name,color,other.result.profilePq,other.targetPq});
            Colour mapped;
            if(result.colour&&colour(request,nits,work,mapped)){
                for(auto pair:{std::make_pair(&result.map,&mapped.map),std::make_pair(&result.sweep,&mapped.sweep)}){
                    if(pair.second->series.empty())continue;
                    auto series=pair.second->series.front();series.name=name;series.color=color;series.bold=false;
                    pair.first->series.push_back(std::move(series));
                }
            }
        }
        return result;
    }
public:
    Service(){pool_.setMaxThreadCount(1);pool_.setThreadPriority(QThread::LowPriority);}
    static Service& Get(){static Service service;return service;}
    void submit(Work work,const std::shared_ptr<Job>& job){
        pool_.start(QRunnable::create([this,work=std::move(work),job]{
            try {job->result=compute(work);}catch(...){job->result.error=QStringLiteral("Chart calculation failed.");}
            job->done.store(true,std::memory_order_release);
        }),1);
    }
    void warm(Work work,std::weak_ptr<int> lifetime){
        QByteArray key=Key(work.request);QDataStream out(&key,QIODevice::Append);out<<work.display<<work.zone<<quint64(work.patch);
        {std::lock_guard<std::mutex> lock(warmMutex_);if(warming_.size()>=128||!warming_.insert(key).second)return;}
        pool_.start(QRunnable::create([this,work=std::move(work),lifetime,key]{
            if(!lifetime.expired()){try{compute(work);}catch(...){}}
            std::lock_guard<std::mutex> lock(warmMutex_);warming_.erase(key);
        }),-1);
    }
    void drain(){pool_.waitForDone();}
};
}
