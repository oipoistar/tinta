#include "footnotes.h"
#include <algorithm>
#include <cctype>

namespace qmd {
namespace {
std::string fold(std::string text) {
    for(auto& c:text) if(static_cast<unsigned char>(c)<128) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
ElementPtr node(ElementType type, Element* parent, const std::string& text={}) {
    auto e=std::make_shared<Element>(type); e->parent=parent; e->text=text; return e;
}
bool labelAt(const std::string& text,size_t start,size_t& end) {
    if(text.compare(start,2,"[^")!=0) return false;
    end=text.find(']',start+2);
    if(end==std::string::npos || end==start+2 || end-start>1000) return false;
    for(size_t i=start+2;i<end;++i)
        if(std::isspace(static_cast<unsigned char>(text[i])) || text[i]=='[') return false;
    return true;
}
}

FootnoteData extractFootnotes(const std::string& source) {
    FootnoteData notes; notes.source=source;
    struct Line { size_t begin,end,next,indent; };
    std::vector<Line> lines;
    for(size_t pos=0;pos<source.size();) {
        size_t eol=source.find('\n',pos), next=eol==std::string::npos ? source.size() : eol+1;
        size_t end=eol==std::string::npos ? source.size() : eol;
        if(end>pos && source[end-1]=='\r')--end;
        size_t first=pos; while(first<end && source[first]==' ')++first;
        lines.push_back({pos,end,next,first-pos}); pos=next;
    }
    char fence=0; size_t fenceLength=0;
    std::string rawEnd;
    for(size_t row=0;row<lines.size();++row) {
        const auto line=lines[row]; size_t first=line.begin+line.indent;
        std::string text=source.substr(first,line.end-first), lowered=fold(text);
        if(!rawEnd.empty()) { if(lowered.find(rawEnd)!=std::string::npos)rawEnd.clear(); continue; }
        if(fence) {
            size_t n=0;while(n<text.size()&&text[n]==fence)++n;
            if(line.indent<=3 && n>=fenceLength && text.find_first_not_of(" \t",n)==std::string::npos) fence=0;
            continue;
        }
        if(line.indent<=3 && !text.empty() && (text[0]=='`'||text[0]=='~')) {
            size_t n=0;while(n<text.size()&&text[n]==text[0])++n;
            if(n>=3) { fence=text[0];fenceLength=n;continue; }
        }
        for(const char* tag: {"pre","script","style","textarea"})
            if(lowered.rfind(std::string("<")+tag,0)==0)rawEnd=std::string("</")+tag+">";
        if(lowered.rfind("<!--",0)==0)rawEnd="-->";
        if(!rawEnd.empty()) { if(lowered.find(rawEnd)!=std::string::npos)rawEnd.clear();continue; }
        size_t close;
        if(line.indent>3 || !labelAt(source,first,close) || close>=line.end || source[close+1]!=':')continue;
        auto label=fold(source.substr(first+2,close-first-2));
        bool duplicate = notes.byLabel.count(label) != 0; // First definition wins.
        FootnoteSource def; def.label=label;def.sourceOffset=first;
        auto addLine=[&](size_t start,const Line& item) {
            for(size_t p=start;p<item.end;++p) { def.body+=source[p];def.offsets.push_back(p); }
            def.body+='\n';def.offsets.push_back(item.end);
        };
        size_t bodyStart=close+2;
        if(bodyStart<line.end && source[bodyStart]==' ')++bodyStart;
        addLine(bodyStart,line);
        size_t last=row;
        for(size_t n=row+1;n<lines.size();) {
            auto next=lines[n];
            size_t nextClose;
            if(next.indent<=3 && labelAt(source,next.begin+next.indent,nextClose) &&
               nextClose<next.end && source[nextClose+1]==':')break;
            if(next.begin+next.indent==next.end) {
                size_t look=n+1;
                while(look<lines.size() && lines[look].begin+lines[look].indent==lines[look].end)++look;
                if(look==lines.size() || (lines[look].indent<2 && source[lines[look].begin]!='\t'))break;
                addLine(next.end,next);last=n++;continue;
            }
            size_t strip=next.indent>=4 ? 4 : next.indent>=2 ? 2 : source[next.begin]=='\t' ? 1 : 0;
            if(!strip)break;
            addLine(next.begin+strip,next);last=n++;
        }
        // Blanking preserves byte offsets for editor/preview synchronization.
        for(size_t p=line.begin;p<lines[last].next;++p)
            if(notes.source[p]!='\n'&&notes.source[p]!='\r')notes.source[p]=' ';
        if (!duplicate) {
            notes.byLabel[label]=notes.definitions.size();notes.definitions.push_back(std::move(def));
        }
        row=last;
    }
    return notes;
}

void appendFootnoteText(Element* parent,const std::string& text,
                        const std::vector<size_t>& literalBrackets,FootnoteData& notes) {
    for(auto p=parent;p;p=p->parent)
        if(p->type==ElementType::Code || p->type==ElementType::CodeBlock || p->type==ElementType::Link || p->type==ElementType::HtmlBlock) {
            parent->children.push_back(node(ElementType::Text,parent,text));return;
        }
    size_t cursor=0;
    for(size_t i=0;i<text.size();++i) {
        size_t end;
        if(!labelAt(text,i,end) || std::binary_search(literalBrackets.begin(),literalBrackets.end(),i))continue;
        auto found=notes.byLabel.find(fold(text.substr(i+2,end-i-2)));
        if(found==notes.byLabel.end())continue;
        auto& def=notes.definitions[found->second];
        if(!def.number) { notes.order.push_back(found->second);def.number=static_cast<int>(notes.order.size()); }
        ++def.references;
        if(i>cursor)parent->children.push_back(node(ElementType::Text,parent,text.substr(cursor,i-cursor)));
        auto ref=node(ElementType::FootnoteReference,parent);
        ref->level=def.number;
        ref->url="#tinta-fn-"+std::to_string(def.number);
        ref->title="tinta-fnref-"+std::to_string(def.number)+"-"+std::to_string(def.references);
        ref->children.push_back(node(ElementType::Text,ref.get(),std::to_string(def.number)));
        parent->children.push_back(ref);cursor=end+1;i=end;
    }
    if(cursor<text.size())parent->children.push_back(node(ElementType::Text,parent,text.substr(cursor)));
}

void appendFootnotes(const ElementPtr& root,FootnoteData& notes,
                     const std::function<ParseResult(const std::string&)>& parseBody) {
    if(notes.order.empty())return;
    auto section=node(ElementType::Footnotes,root.get());
    // Definitions render once, in the order of their first document reference.
    for(size_t i=0;i<notes.order.size();++i) {
        auto& def=notes.definitions[notes.order[i]];
        auto parsed=parseBody(def.body);
        auto note=node(ElementType::FootnoteDefinition,section.get());
        note->level=def.number;note->url="tinta-fn-"+std::to_string(def.number);
        note->sourceOffset=def.sourceOffset;
        if(parsed.root) {
            std::function<void(const ElementPtr&)> map=[&](const ElementPtr& e) {
                if(e->sourceOffset<def.offsets.size())e->sourceOffset=def.offsets[e->sourceOffset];
                for(auto& child:e->children)map(child);
            };
            for(auto& child:parsed.root->children) { map(child);child->parent=note.get();note->children.push_back(child); }
        }
        section->children.push_back(note);
    }
    for(size_t i=0;i<section->children.size();++i) {
        auto& note=section->children[i];auto& def=notes.definitions[notes.order[i]];
        auto back=node(ElementType::Paragraph,note.get());
        back->language="footnote-backlinks";
        if(def.references>1) back->children.push_back(node(ElementType::Text,back.get(),"Back to references: "));
        for(int r=1;r<=def.references;++r) {
            if(r>1)back->children.push_back(node(ElementType::Text,back.get(),", "));
            auto link=node(ElementType::FootnoteBacklink,back.get());
            link->url="#tinta-fnref-"+std::to_string(def.number)+"-"+std::to_string(r);
            link->children.push_back(node(ElementType::Text,link.get(),def.references==1 ? "Back to reference" : std::to_string(r)));
            back->children.push_back(link);
        }
        note->children.push_back(back);
    }
    root->children.push_back(section);
}
}
