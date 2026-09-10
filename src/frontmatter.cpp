#include "frontmatter.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>
#include <windows.h>

namespace fm {
namespace {
std::string trim(const std::string& s) {
    auto a=s.find_first_not_of(" \t\r"),b=s.find_last_not_of(" \t\r");
    return a==std::string::npos ? "" : s.substr(a,b-a+1);
}
bool scalar(const std::string& raw,std::string& value) {
    value=trim(raw);
    if(value.empty())return true;
    char quote=value.front();
    if(quote=='\'' || quote=='"') {
        std::string out; bool closed=false;
        for(size_t i=1;i<value.size();++i) {
            char c=value[i];
            if(c==quote) {
                if(quote=='\'' && i+1<value.size() && value[i+1]=='\'') {out+='\'';++i;continue;}
                if(i!=value.size()-1)return false;
                closed=true;break;
            }
            if(c=='\\' && quote=='"') {
                if(++i==value.size())return false;
                c=value[i];
                if(c=='n')c='\n';else if(c=='t')c='\t';
                else if(c!='"'&&c!='\\'&&c!='/')return false;
            }
            out+=c;
        }
        if(!closed)return false;
        value=out;return true;
    }
    if(value.find_first_of("[]{}")!=std::string::npos || value.find(": ")!=std::string::npos ||
       value.find_first_of("&*!|>")==0)return false;
    return true;
}
// Comment delimiters only count outside quotes and after whitespace.
size_t commentAt(const std::string& line) {
    char quote=0;
    for(size_t i=0;i<line.size();++i) {
        char c=line[i];
        if(quote) {
            if(c=='\\'&&quote=='"') {++i;continue;}
            if(c==quote) {
                if(c=='\''&&i+1<line.size()&&line[i+1]=='\''){++i;continue;}
                quote=0;
            }
        } else if((c=='\''||c=='"') && (i==0 || line[i-1]==' ' || line[i-1]=='[' || line[i-1]==','))quote=c;
        else if(c=='#'&&(i==0||line[i-1]==' '||line[i-1]=='\t'))return i;
    }
    return line.size();
}
bool list(const std::string& raw,std::vector<std::string>& out) {
    auto v=trim(raw);
    if(v.size()<2||v.front()!='['||v.back()!=']')return false;
    char quote=0;size_t start=1;
    for(size_t i=1;i<v.size();++i) {
        char c=v[i];
        if(quote) {
            if(c=='\\'&&quote=='"'){++i;continue;}
            if(c==quote) {
                if(c=='\''&&i+1<v.size()&&v[i+1]=='\''){++i;continue;}
                quote=0;
            }
        } else if(c=='"'||c=='\'')quote=c;
        else if(c==','||i==v.size()-1) {
            std::string item;
            if(!scalar(v.substr(start,i-start),item))return false;
            if(!item.empty())out.push_back(item);
            start=i+1;
        }
    }
    return !quote;
}
bool dateParts(const std::string& text,SYSTEMTIME& st) {
    int y=0,m=0,d=0,h=0,mi=0,se=0;
    if(text.size()<10||text[4]!='-'||text[7]!='-'||sscanf_s(text.c_str(),"%d-%d-%d",&y,&m,&d)!=3)return false;
    if(text.size()>10) {
        if(text[10]!='T'&&text[10]!=' ')return false;
        if(sscanf_s(text.c_str()+11,"%d:%d:%d",&h,&mi,&se)<2)return false;
    }
    if(y<1601||y>9999||m<1||m>12||d<1||d>31||h<0||mi<0||se<0||h>23||mi>59||se>59)return false;
    st={};st.wYear=(WORD)y;st.wMonth=(WORD)m;st.wDay=(WORD)d;
    st.wHour=(WORD)h;st.wMinute=(WORD)mi;st.wSecond=(WORD)se;
    FILETIME ft{};return SystemTimeToFileTime(&st,&ft)!=0;
}
void infer(Property& p) {
    SYSTEMTIME st{};
    if(dateParts(p.text,st))p.kind=Kind::Date;
    else if(p.text=="true"||p.text=="false")p.kind=Kind::Boolean;
}
}

Document parse(const std::string& source) {
    Document doc;
    doc.begin=source.compare(0,3,"\xEF\xBB\xBF")==0?3:0;
    size_t eol=source.find('\n',doc.begin);
    auto opener = eol == std::string::npos ? "" : source.substr(doc.begin,eol-doc.begin);
    if(!opener.empty() && opener.back()=='\r')opener.pop_back();
    if(eol==std::string::npos||opener!="---")return doc;
    if(eol>0&&source[eol-1]=='\r')doc.eol="\r\n";
    struct Line {size_t start,end,next;};std::vector<Line> lines;
    for(size_t at=eol+1;at<=source.size();) {
        auto next=source.find('\n',at);size_t end=next==std::string::npos?source.size():next;
        if(end>at&&source[end-1]=='\r')--end;
        auto text=source.substr(at,end-at);
        if(text=="---"||text=="...") {doc.present=true;doc.closing=at;doc.end=next==std::string::npos?source.size():next+1;break;}
        lines.push_back({at,end,next==std::string::npos?source.size():next+1});
        if(next==std::string::npos)break;at=next+1;
    }
    if(!doc.present)return doc;
    std::set<std::string> seen;
    for(size_t row=0;row<lines.size();++row) {
        const auto& l=lines[row];auto raw=source.substr(l.start,l.end-l.start);
        auto text=trim(raw);
        if(text.empty()||text[0]=='#')continue;
        if(raw.front()==' '||raw.front()=='\t')continue;
        auto colon=raw.find(':');
        if(colon==std::string::npos||colon==0 || (colon+1<raw.size()&&raw[colon+1]!=' '&&raw[colon+1]!='\t')) {
            doc.safeToWrite=false;continue;
        }
        Property p;
        if(!scalar(trim(raw.substr(0,colon)),p.key)||p.key.empty()){doc.safeToWrite=false;continue;}
        if(!seen.insert(p.key).second){doc.safeToWrite=false;continue;}
        size_t start=colon+1;while(start<raw.size()&&(raw[start]==' '||raw[start]=='\t'))++start;
        auto valueRaw=raw.substr(start);size_t comment=commentAt(valueRaw);
        auto value=trim(valueRaw.substr(0,comment));
        p.valueStart=l.start+start;p.valueEnd=p.valueStart+value.size();
        p.writable=scalar(value,p.text);
        p.nullValue=value=="null"||value=="Null"||value=="NULL"||value=="~";
        if(p.key=="<<" || (!p.writable && !value.empty() && (value[0]=='\'' || value[0]=='"' || value[0]=='{')))
            doc.safeToWrite=false;
        if(!value.empty()&&value[0]=='[') {
            p.items.clear();
            if(list(value,p.items)){p.kind=Kind::List;p.text.clear();}else {p.kind=Kind::Structured;doc.safeToWrite=false;}
            p.writable=false;
        } else if(!p.writable)p.kind=Kind::Structured;
        // A nested mapping or block scalar stays opaque. Never rewrite it as a date.
        size_t next=row+1;std::vector<std::string> items;bool children=false,allItems=true;
        while(next<lines.size()) {
            auto v=source.substr(lines[next].start,lines[next].end-lines[next].start);auto t=trim(v);
            if(t.empty()||t[0]=='#'){++next;continue;}
            if(v[0]!=' '&&v[0]!='\t'&&t.rfind("- ",0)!=0)break;
            children=true;std::string item;
            if(t.rfind("- ",0)==0&&scalar(trim(t.substr(2,commentAt(t.substr(2)))),item))items.push_back(item);
            else allItems=false;
            ++next;
        }
        if(children) {
            p.writable=false;
            if(value.empty()&&allItems){p.kind=Kind::List;p.items=std::move(items);p.text.clear();}
            else p.kind=Kind::Structured;
            row=next-1;
        }
        if(p.kind==Kind::Text)infer(p);
        if(p.kind==Kind::Structured)p.text="(structured value)";
        doc.properties.push_back(std::move(p));
    }
    return doc;
}
const Property* find(const Document& doc,const std::string& key) {
    for(const auto& p:doc.properties)if(p.key==key)return &p;return nullptr;
}
const Rule* find(const Settings& settings,const std::string& key) {
    for(const auto& r:settings.rules)if(r.key==key)return &r;return nullptr;
}
bool discover(Settings& settings,const std::vector<Property>& properties) {
    bool changed=false;
    for(const auto& p:properties) {
        auto it=std::find_if(settings.rules.begin(),settings.rules.end(),[&](const Rule&r){return r.key==p.key;});
        if(it==settings.rules.end() && settings.rules.size()<256) {
            auto f=p.kind==Kind::Date?Format::Absolute:p.kind==Kind::List?Format::Chips:Format::Plain;
            settings.rules.push_back({p.key,settings.showOther,false,true,f,5,p.kind});changed=true;
        } else if(it!=settings.rules.end()&&it->hint!=p.kind) {it->hint=p.kind;changed=true;}
    }
    return changed;
}
bool maintained(const Settings& settings,const std::string& key) {
    auto r=find(settings,key);return settings.shown&&r&&r->show;
}
std::vector<Format> formats(Kind kind) {
    if(kind==Kind::List)return {Format::Chips,Format::Hashtags,Format::Comma,Format::Count};
    if(kind==Kind::Date)return {Format::Absolute,Format::Relative,Format::Plain};
    if(kind==Kind::Text)return {Format::Plain,Format::Large};
    return {Format::Plain};
}
std::string utcNow() {
    SYSTEMTIME st;GetSystemTime(&st);char value[32];
    sprintf_s(value,"%04u-%02u-%02uT%02u:%02u:%02uZ",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
    return value;
}
std::string formatValue(const Property& p,Format format,const std::string& now) {
    if(p.kind==Kind::List) {
        if(format==Format::Count)return std::to_string(p.items.size())+" items";
        std::string out;for(const auto& item:p.items){if(!out.empty())out+=", ";out+=item;}return out;
    }
    SYSTEMTIME st{},current{};
    if(p.kind==Kind::Date&&dateParts(p.text,st)) {
        if(format==Format::Absolute) {
            static const char* months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            return std::string(months[st.wMonth-1])+" "+std::to_string(st.wDay)+", "+std::to_string(st.wYear);
        }
        if(format==Format::Relative&&dateParts(now,current)) {
            FILETIME a{},b{};SystemTimeToFileTime(&st,&a);SystemTimeToFileTime(&current,&b);
            auto ticks=[](FILETIME t){return (static_cast<long long>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
            auto offset=[](const std::string& text) -> long long {
                // RFC 3339 offset; the displayed calendar date stays as written.
                auto sign=text.find_first_of("+-",10);
                if(sign==std::string::npos)return 0;
                int h=0,m=0;
                if(sscanf_s(text.c_str()+sign+1,"%d:%d",&h,&m)!=2||h>23||m>59)return 0;
                return (text[sign]=='-'?-1:1)*(h*3600LL+m*60LL);
            };
            long long delta=(ticks(b)-ticks(a))/10000000+offset(p.text)-offset(now),absolute=delta<0?-delta:delta;
            if(absolute<60)return "just now";
            long long n=absolute<3600?absolute/60:absolute<86400?absolute/3600:absolute/86400;
            std::string unit=absolute<3600?"minute":absolute<86400?"hour":"day";
            return (delta<0?"in ":"")+std::to_string(n)+" "+unit+(n==1?"":"s")+(delta>=0?" ago":"");
        }
    }
    return p.text;
}
std::string stamp(const std::string& source,const Settings& settings,const std::string& firstSeen,const std::string& now) {
    auto doc=parse(source);if(!doc.present||!doc.safeToWrite)return source;
    struct Edit{size_t start,end;std::string value;};std::vector<Edit> edits;std::string added;
    for(const char* key:{"created","updated"}) {
        if(!maintained(settings,key))continue;
        auto p=find(doc,key);
        if(p&&(!p->writable||(std::string(key)=="created"&&!p->text.empty()&&!p->nullValue)))continue;
        const auto& value=std::string(key)=="created"&&!firstSeen.empty()?firstSeen:now;
        std::string quoted="\""+value+"\"";
        if(p) {
            // A key ending directly at ':' needs its YAML separation space.
            if(p->valueStart>0&&source[p->valueStart-1]==':')quoted=" "+quoted;
            if(p->valueStart==p->valueEnd && p->valueEnd<source.size() && source[p->valueEnd]=='#')quoted+=' ';
            edits.push_back({p->valueStart,p->valueEnd,quoted});
        } else added+=std::string(key)+": "+quoted+doc.eol;
    }
    if(!added.empty())edits.push_back({doc.closing,doc.closing,added});
    std::sort(edits.begin(),edits.end(),[](const Edit&a,const Edit&b){return a.start>b.start;});
    auto out=source;for(const auto&e:edits)out.replace(e.start,e.end-e.start,e.value);return out;
}
std::string encodeRule(const Rule& r) {
    static const char* hex="0123456789ABCDEF";std::string key;
    for(unsigned char c:r.key){key+=hex[c>>4];key+=hex[c&15];}
    return key+","+std::to_string(r.show)+","+std::to_string(r.right)+","+std::to_string(r.label)+","+
        std::to_string((int)r.format)+","+std::to_string(r.limit)+","+std::to_string((int)r.hint);
}
bool decodeRule(const std::string& value,Rule& r) {
    std::vector<std::string> parts;std::stringstream ss(value);std::string p;
    while(std::getline(ss,p,','))parts.push_back(p);
    if(parts.size()!=7||parts[0].empty()||parts[0].size()>2048||parts[0].size()%2)return false;
    Rule out;try {
        for(size_t i=0;i<parts[0].size();i+=2) {
            auto pair=parts[0].substr(i,2);size_t n=0;int c=std::stoi(pair,&n,16);
            if(n!=2||c==0)return false;out.key+=(char)c;
        }
        int values[6];for(int i=0;i<6;++i){size_t n;values[i]=std::stoi(parts[i+1],&n);if(n!=parts[i+1].size())return false;}
        if(values[0]<0||values[0]>1||values[1]<0||values[1]>1||values[2]<0||values[2]>1||values[3]<0||values[3]>7||values[4]<0||values[4]>100||values[5]<0||values[5]>4)return false;
        out.show=values[0]!=0;out.right=values[1]!=0;out.label=values[2]!=0;out.format=(Format)values[3];out.limit=values[4];out.hint=(Kind)values[5];
    }catch(...){return false;}r=std::move(out);return true;
}
}
