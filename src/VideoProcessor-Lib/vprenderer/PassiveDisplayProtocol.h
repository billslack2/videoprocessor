#pragma once
#include <cstdint>
#include <cstring>
#include <limits>

// Strict, bounded parser for the private observer pipe. Malformed/unknown data
// is a coverage failure, never a zero counter or a fabricated display event.
namespace PassiveDisplayProtocol {
enum class Kind { Hello, Status, Event, Error };
struct Message {
    Kind kind = Kind::Error;
    uint32_t version=0, status=0, processId=0, threadId=0, disposition=0, mode=0, flags=0;
    uint64_t frequency=0, eventsLost=0, buffersLost=0, overflows=0, address=0, start=0, screen=0;
    bool lossKnown=true;
};
class Reader {
    const char* cursor;
public:
    explicit Reader(const char* value) : cursor(value) {}
    bool Word(const char* value) {
        const auto n=std::strlen(value);
        if(std::strncmp(cursor,value,n)!=0 || cursor[n]!=' ') return false;
        cursor+=n+1; return true;
    }
    bool Number(uint64_t& value, unsigned base=10) {
        while(*cursor==' ') ++cursor;
        if(base==16) {
            if(cursor[0]!='0' || (cursor[1]!='x' && cursor[1]!='X')) return false;
            cursor+=2;
        }
        value=0; bool any=false;
        for(;;) {
            const unsigned char c=static_cast<unsigned char>(*cursor);
            const unsigned digit=c>='0' && c<='9' ? c-'0' :
                base==16 && c>='a' && c<='f' ? c-'a'+10 :
                base==16 && c>='A' && c<='F' ? c-'A'+10 : 255;
            if(digit>=base) break;
            if(value>((std::numeric_limits<uint64_t>::max)()-digit)/base) return false;
            value=value*base+digit; ++cursor; any=true;
        }
        return any && (*cursor==' ' || !*cursor);
    }
    bool Number(uint32_t& value, unsigned base=10) {
        uint64_t wide=0;
        if(!Number(wide,base) || wide>UINT32_MAX) return false;
        value=static_cast<uint32_t>(wide); return true;
    }
    bool Loss(uint64_t& value, bool& known) {
        while(*cursor==' ') ++cursor;
        if(cursor[0]=='-' && cursor[1]=='1' && (cursor[2]==' ' || !cursor[2])) {
            cursor+=2; value=0; known=false; return true;
        }
        return Number(value);
    }
    bool End() { while(*cursor==' ') ++cursor; return !*cursor; }
};
inline bool Parse(const char* line, Message& out) {
    if(!line) return false;
    size_t size=0; while(size<256 && line[size]) ++size;
    if(!size || size==256) return false;
    Message next;
    Reader hello(line), status(line), event(line), error(line);
    if(hello.Word("HELLO")) {
        next.kind=Kind::Hello;
        if(!hello.Number(next.version) || !hello.Number(next.frequency) || !hello.End() || !next.frequency || next.frequency>INT64_MAX) return false;
    } else if(status.Word("STATUS")) {
        next.kind=Kind::Status;
        if(!status.Number(next.status) || !status.Loss(next.eventsLost,next.lossKnown) ||
            !status.Loss(next.buffersLost,next.lossKnown) || !status.Loss(next.overflows,next.lossKnown) || !status.End()) return false;
    } else if(event.Word("EVENT")) {
        next.kind=Kind::Event;
        if(!event.Number(next.processId) || !event.Number(next.threadId) || !event.Number(next.address,16) ||
            !event.Number(next.start) || !event.Number(next.screen) || !event.Number(next.disposition) ||
            !event.Number(next.mode) || !event.Number(next.flags,16) || !event.End() || next.start>INT64_MAX ||
            next.screen>INT64_MAX || next.disposition>2) return false;
    } else if(error.Word("ERROR")) {
        next.kind=Kind::Error;
        if(!error.Number(next.status) || !error.End()) return false;
    } else return false;
    out=next; return true;
}
}
