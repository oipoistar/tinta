#include "frontmatter_ui.h"
#include "settings.h"
#include "utils.h"
#include "i18n.h"
#include <algorithm>
#include <cmath>

namespace {
bool hit(const D2D1_RECT_F& r,float x,float y) {return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
D2D1_COLOR_F color(D2D1_COLOR_F c,float alpha) {c.a*=alpha;return c;}
std::wstring label(std::string key) {if(!key.empty()&&key[0]>='a'&&key[0]<='z')key[0]-=32;return toWide(key);}
const char* formatKey(fm::Format f) {
    const char* keys[]={"fm.plain","fm.large","fm.absolute","fm.relative","fm.chips","fm.hashtags","fm.comma","fm.count"};
    return keys[(int)f];
}
const wchar_t* kindName(fm::Kind k) {
    const wchar_t* names[]={L"text",L"date",L"list",L"bool",L"object"};return names[(int)k];
}
void fill(App& app,D2D1_RECT_F r,D2D1_COLOR_F c,float radius=5) {
    app.brush->SetColor(c);app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),app.brush);
}
void text(App& app,const std::wstring& value,D2D1_RECT_F r,float size,D2D1_COLOR_F c,bool bold=false,bool right=false) {
    IDWriteTextLayout* layout=nullptr;
    app.dwriteFactory->CreateTextLayout(value.c_str(),(UINT32)value.size(),app.folderBrowserFormat,
        std::max(1.f,r.right-r.left),std::max(1.f,r.bottom-r.top),&layout);
    if(!layout)return;
    layout->SetFontSize(size,{0,(UINT32)value.size()});
    if(bold)layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD,{0,(UINT32)value.size()});
    if(right)layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
    IDWriteInlineObject* ellipsis=nullptr;app.dwriteFactory->CreateEllipsisTrimmingSign(app.folderBrowserFormat,&ellipsis);
    layout->SetTrimming(&trimming,ellipsis);if(ellipsis)ellipsis->Release();
    app.brush->SetColor(c);app.renderTarget->DrawTextLayout({r.left,r.top},layout,app.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);layout->Release();
}
void button(App& app,D2D1_RECT_F r,const std::wstring& value,int action,bool active=false,bool enabled=true) {
    float scale=app.frontmatterUiScale;
    fill(app,r,color(active?app.theme.accent:app.theme.text,active?.16f:.045f),4*scale);
    text(app,value,{r.left+7*scale,r.top,r.right-5*scale,r.bottom},11*scale,
         color(active?app.theme.accent:app.theme.text,enabled?1.f:.3f),active);
    if(enabled)app.settingsHits.push_back({r,action});
}
std::vector<fm::Property> previewProperties(const App& app) {
    if(app.root)for(const auto&e:app.root->children)if(e->type==ElementType::Properties&&!e->properties.empty())return e->properties;
    return fm::parse("---\ntitle: Proxmox Setup\nauthor: Jane Doe\ntags: [proxmox, homelab, tailscale, server, notes]\ncreated: 2026-01-03\nupdated: 2026-09-08\ndraft: false\n---\n").properties;
}
}

std::string frontmatterSeenKey(const App& app) {
    if(!app.currentFile.empty()) {
        auto key=app.currentFile;for(auto&c:key)if(c>='A'&&c<='Z')c+=32;return key;
    }
    return "untitled:"+std::to_string(app.activeTab>=0&&app.activeTab<(int)app.tabs.size()?app.tabs[app.activeTab].id:0);
}
void persistFrontmatter(const App& app) {
    auto settings=loadSettings();settings.frontmatter=app.frontmatter;saveSettings(settings);
}
void observeFrontmatter(App& app,const std::vector<fm::Property>& properties) {
    auto key=frontmatterSeenKey(app);
    if(!app.frontmatterFirstSeen.count(key))app.frontmatterFirstSeen.emplace(key,fm::utcNow());
    if(fm::discover(app.frontmatter,properties))persistFrontmatter(app);
}

float layoutFrontmatterStrip(App& app,const std::vector<fm::Property>& properties,
                             float x,float y,float width,float scale,bool preview) {
    if(!app.frontmatter.shown||width<=0)return y;
    struct Part {App::LayoutTextRun run;bool chip=false;std::wstring overflow;};std::vector<Part> parts;
    auto now=fm::utcNow();float top=y,gap=7*scale,rowGap=5*scale;
    bool twoColumns=width>=520*scale;
    // The miniature preview follows the current document's responsive state.
    if(preview && app.layoutMaxWidth>0)
        twoColumns=app.layoutMaxWidth>=520*app.contentScale*app.zoomFactor;
    std::vector<const fm::Rule*> groups[2];
    std::vector<fm::Rule> extras;
    for(const auto&r:app.frontmatter.rules) {
        auto p=std::find_if(properties.begin(),properties.end(),[&](const fm::Property&p){return p.key==r.key;});
        if(r.show&&p!=properties.end())groups[r.right?1:0].push_back(&r);
    }
    // Freshly parsed documents can render before their discovery is persisted.
    if(app.frontmatter.showOther)for(const auto&p:properties)if(!fm::find(app.frontmatter,p.key))
        extras.push_back({p.key,true,false,true,fm::Format::Plain,5,p.kind});
    for(const auto&r:extras)groups[0].push_back(&r);
    bool hasLeft=!groups[0].empty(),hasRight=!groups[1].empty();
    float heights[2]={0,0};
    for(int side=0;side<2;++side) {
        float w=twoColumns&&hasLeft&&hasRight?(side?width*.34f:width*.66f-gap*2):width;
        float left=x+(side&&twoColumns&&hasLeft?width-w:0);
        float gy=top+(side&&!twoColumns&&hasLeft?heights[0]+rowGap:0),gx=left,lineH=0;
        size_t lineBegin=parts.size();
        auto finish=[&]() {
            if(side&&twoColumns) {
                float dx=left+w-(gx-gap);
                for(size_t i=lineBegin;i<parts.size();++i){parts[i].run.pos.x+=dx;parts[i].run.bounds.left+=dx;parts[i].run.bounds.right+=dx;}
            }
            gy+=lineH+rowGap;gx=left;lineH=0;lineBegin=parts.size();
        };
        auto add=[&](const std::wstring& value,bool chip,bool large,D2D1_COLOR_F tint,const std::wstring& overflow=L"") {
            if(value.empty())return;
            float pad=chip?6*scale:0;
            IDWriteTextLayout* layout=nullptr;
            app.dwriteFactory->CreateTextLayout(value.c_str(),(UINT32)value.size(),app.textFormat,std::max(1.f,w-pad*2),10000.f,&layout);
            if(!layout)return;
            layout->SetFontSize((large?18.f:12.f)*scale,{0,(UINT32)value.size()});
            if(large)layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD,{0,(UINT32)value.size()});
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            DWRITE_TEXT_METRICS m{};layout->GetMetrics(&m);
            float itemW=std::min(w,m.widthIncludingTrailingWhitespace+pad*2),itemH=m.height+(chip?4*scale:0);
            if((large||gx+itemW>left+w+.1f)&&gx>left)finish();
            D2D1_RECT_F bounds={gx,gy,gx+itemW,gy+itemH};
            parts.push_back({{layout,{gx+pad,gy+(chip?2*scale:0)},bounds,tint,0,0,false},chip,overflow});
            gx+=itemW+gap;lineH=std::max(lineH,itemH);
            if(large)finish();
        };
        for(const auto*r:groups[side]) {
            const auto& p=*std::find_if(properties.begin(),properties.end(),[&](const fm::Property&p){return p.key==r->key;});
            if(side&&gx>left)finish();
            auto options=fm::formats(p.kind);auto format=std::find(options.begin(),options.end(),r->format)==options.end()?options.front():r->format;
            bool large=format==fm::Format::Large;
            if(p.kind==fm::Kind::List&&format!=fm::Format::Count) {
                if(r->label)add(label(p.key)+L":",false,false,color(app.theme.text,.6f));
                size_t count=r->limit?std::min(p.items.size(),(size_t)r->limit):p.items.size();
                for(size_t i=0;i<count;++i) {
                    std::wstring item=(format==fm::Format::Hashtags?L"#":L"")+toWide(p.items[i]);
                    if(format==fm::Format::Comma&&i+1<count)item+=L",";
                    add(item,format==fm::Format::Chips,false,format==fm::Format::Comma?color(app.theme.text,.8f):app.theme.accent);
                }
                if(count<p.items.size()) {
                    std::wstring hidden;for(size_t i=count;i<p.items.size();++i){if(!hidden.empty())hidden+=L", ";hidden+=toWide(p.items[i]);}
                    add(L"+"+std::to_wstring(p.items.size()-count),true,false,app.theme.accent,hidden);
                }
            } else {
                auto value=toWide(fm::formatValue(p,format,now));
                if(r->label)value=label(p.key)+L": "+value;
                add(value,false,large,color(large?app.theme.heading:app.theme.text,large?1.f:.75f));
            }
        }
        if(lineH>0)finish();heights[side]=gy-top;
    }
    float bottom=std::max(heights[0],heights[1])+top;
    if(parts.empty())return top;
    for(auto&part:parts) {
        auto&run=part.run;
        if(preview) {
            if(part.chip)fill(app,run.bounds,color(app.theme.accent,.12f),5*scale);
            app.brush->SetColor(run.color);app.renderTarget->DrawTextLayout(run.pos,run.layout,app.brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);run.layout->Release();
        } else {
            if(!part.overflow.empty())app.frontmatterOverflow.push_back({run.bounds,part.overflow});
            if(part.chip) {
                App::LayoutShape shape;shape.type=App::LayoutShapeType::RoundedRectangle;shape.rect=run.bounds;
                shape.fill=color(app.theme.accent,.12f);shape.stroke={0,0,0,0};shape.strokeWidth=0;shape.radius=5*scale;
                app.layoutShapes.push_back(shape);
            }
            app.layoutTextRuns.push_back(run);
        }
    }
    if(!preview)app.layoutLines.push_back({{x,bottom+7*scale},{x+width,bottom+7*scale},color(app.theme.text,.12f),scale});
    return bottom+22*scale;
}

void renderFrontmatterSettings(App& app,const D2D1_RECT_F& area) {
    float x=area.left,y=area.top,w=area.right-x;
    float s=std::min({app.contentScale, w/360.f, (area.bottom-area.top)/390.f});
    app.frontmatterUiScale=s;
    auto normal=app.theme.text,muted=color(normal,.5f),accent=app.theme.accent;
    auto card=color(normal,app.theme.isDark?.045f:.028f);
    fill(app,{x,y,x+w,y+58*s},card,6*s);
    text(app,tr(app,"fm.title"),{x+12*s,y+5*s,x+w-140*s,y+29*s},13*s,normal,true);
    text(app,tr(app,"fm.strip_hint"),{x+12*s,y+29*s,x+w-10*s,y+51*s},11*s,muted);
    button(app,{x+w-136*s,y+8*s,x+w-72*s,y+32*s},tr(app,"fm.hidden"),FM_HIDDEN,!app.frontmatter.shown);
    button(app,{x+w-71*s,y+8*s,x+w-8*s,y+32*s},tr(app,"fm.shown"),FM_SHOWN,app.frontmatter.shown);
    y+=66*s;
    text(app,tr(app,"fm.save_hint"),{x,y,x+w,y+22*s},11*s,muted);
    y+=27*s;
    float previewH=98*s,footer=44*s;
    float listBottom=std::max(y+66*s,area.bottom-previewH-footer-28*s);
    float headerH=25*s,rowH=37*s;
    float keyW=std::max(74*s,w*.26f),showX=x+keyW,sideX=showX+36*s,labelX=sideX+55*s,formatX=labelX+44*s;
    fill(app,{x,y,x+w,listBottom},card,5*s);
    text(app,tr(app,"fm.key"),{x+24*s,y,showX,y+headerH},9*s,muted,true);
    text(app,tr(app,"fm.show"),{showX,y,sideX,y+headerH},9*s,muted,true);
    text(app,tr(app,"fm.side"),{sideX,y,labelX,y+headerH},9*s,muted,true);
    text(app,tr(app,"fm.label"),{labelX,y,formatX,y+headerH},9*s,muted,true);
    text(app,tr(app,"fm.format"),{formatX,y,x+w,y+headerH},9*s,muted,true);
    app.frontmatterListRect={x,y+headerH,x+w,listBottom};
    float visible=listBottom-y-headerH;
    app.frontmatterScroll=std::clamp(app.frontmatterScroll,0.f,std::max(0.f,rowH*(float)app.frontmatter.rules.size()-visible));
    app.frontmatterRows.assign(app.frontmatter.rules.size(),{});
    app.renderTarget->PushAxisAlignedClip(app.frontmatterListRect,D2D1_ANTIALIAS_MODE_ALIASED);
    for(size_t i=0;i<app.frontmatter.rules.size();++i) {
        auto&r=app.frontmatter.rules[i];float ry=y+headerH+(float)i*rowH-app.frontmatterScroll;
        D2D1_RECT_F row={x,ry,x+w,ry+rowH};app.frontmatterRows[i]=row;
        if(row.bottom<=app.frontmatterListRect.top||row.top>=listBottom)continue;
        bool enabled=app.frontmatter.shown,details=enabled&&r.show;
        auto tc=color(normal,enabled?(r.show?1.f:.45f):.3f);
        if((int)i==app.frontmatterDrag)fill(app,row,color(accent,.1f),0);
        text(app,L"\u2261",{x+6*s,ry,x+21*s,ry+rowH},12*s,muted);
        text(app,toWide(r.key),{x+25*s,ry,showX-36*s,ry+rowH},12*s,tc,true);
        text(app,kindName(r.hint),{showX-33*s,ry,showX-3*s,ry+rowH},8*s,color(tc,.5f));
        int id=FM_ROW+(int)i*8;
        auto clippedHit=[&](D2D1_RECT_F bounds,int action) {
            bounds.top=std::max(bounds.top,app.frontmatterListRect.top);bounds.bottom=std::min(bounds.bottom,listBottom);
            if(bounds.bottom>bounds.top)app.settingsHits.push_back({bounds,action});
        };
        if(enabled)clippedHit({x,ry,x+24*s,ry+rowH},id);
        D2D1_RECT_F box={showX+7*s,ry+11*s,showX+21*s,ry+25*s};
        fill(app,box,color(r.show?accent:normal,r.show&&enabled?1.f:.10f),3*s);
        if(r.show)text(app,L"\u2713",{box.left+2*s,box.top-1*s,box.right,box.bottom},11*s,color(app.theme.background,enabled?1.f:.3f),true);
        if(enabled)clippedHit({showX,ry,sideX,ry+rowH},id+1);
        size_t before=app.settingsHits.size();
        button(app,{sideX,ry+7*s,sideX+25*s,ry+30*s},L"L",id+2,!r.right,details);
        button(app,{sideX+25*s,ry+7*s,sideX+50*s,ry+30*s},L"R",id+3,r.right,details);
        button(app,{labelX+2*s,ry+7*s,labelX+34*s,ry+30*s},r.label?L"On":L"Off",id+4,r.label,details);
        auto opts=fm::formats(r.hint);auto f=std::find(opts.begin(),opts.end(),r.format)==opts.end()?opts.front():r.format;
        std::wstring desc=tr(app,formatKey(f));
        if(r.hint==fm::Kind::List&&f!=fm::Format::Count&&r.limit)desc+=L" \u00b7 "+std::to_wstring(r.limit);
        button(app,{formatX,ry+6*s,x+w-8*s,ry+31*s},desc+L"  \u2304",id+5,false,details);
        for(size_t n=before;n<app.settingsHits.size();++n) {
            auto&rect=app.settingsHits[n].first;rect.top=std::max(rect.top,app.frontmatterListRect.top);rect.bottom=std::min(rect.bottom,listBottom);
        }
        app.brush->SetColor(color(normal,.07f));app.renderTarget->DrawLine({x,ry+rowH},{x+w,ry+rowH},app.brush,s);
    }
    if(app.frontmatterDrag>=0&&app.frontmatterDrop>=0) {
        float dropY=y+headerH+app.frontmatterDrop*rowH-app.frontmatterScroll;
        app.brush->SetColor(accent);app.renderTarget->DrawLine({x+6*s,dropY},{x+w-6*s,dropY},app.brush,2*s);
    }
    app.renderTarget->PopAxisAlignedClip();
    // Partly visible rows must not leave inverted hit boxes for clipped controls.
    app.settingsHits.erase(std::remove_if(app.settingsHits.begin(), app.settingsHits.end(),
        [](const auto& hit) { const auto& r=hit.first; return r.bottom<=r.top || r.right<=r.left; }), app.settingsHits.end());
    if(rowH*app.frontmatter.rules.size()>visible) {
        float thumb=std::max(15*s,visible*visible/(rowH*app.frontmatter.rules.size()));
        float ty=app.frontmatterListRect.top+app.frontmatterScroll/(rowH*app.frontmatter.rules.size()-visible)*(visible-thumb);
        fill(app,{x+w-3*s,ty,x+w,ty+thumb},color(normal,.22f),s);
    }
    float otherY=listBottom+5*s;
    text(app,tr(app,"fm.other"),{x+12*s,otherY,x+w-110*s,otherY+30*s},11*s,muted);
    button(app,{x+w-104*s,otherY+2*s,x+w-8*s,otherY+28*s},tr(app,app.frontmatter.showOther?"fm.shown":"fm.hidden"),FM_OTHER,false,app.frontmatter.shown);
    float py=otherY+39*s;
    text(app,tr(app,"fm.preview"),{x,py,x+w,py+18*s},9*s,muted,true);py+=22*s;
    D2D1_RECT_F preview={x,py,x+w,area.bottom-15*s};
    fill(app,preview,color(normal,.035f),5*s);
    app.renderTarget->PushAxisAlignedClip(preview,D2D1_ANTIALIAS_MODE_ALIASED);
    if(app.frontmatter.shown)layoutFrontmatterStrip(app,previewProperties(app),x+12*s,py+9*s,w-24*s,s*.85f,true);
    else text(app,tr(app,"fm.hidden_preview"),{x+12*s,py,x+w-12*s,preview.bottom},11*s,muted);
    app.renderTarget->PopAxisAlignedClip();
    // A type-specific format menu floats above the table, like other Settings menus.
    app.frontmatterMenuRect={};
    if(app.frontmatterMenu>=0&&app.frontmatterMenu<(int)app.frontmatter.rules.size()) {
        const auto&r=app.frontmatter.rules[app.frontmatterMenu];auto opts=fm::formats(r.hint);
        float menuW=std::min(w,210*s),menuH=(float)opts.size()*29*s+12*s+(r.hint==fm::Kind::List?45*s:0);
        auto row=app.frontmatterRows[app.frontmatterMenu];float my=std::clamp(row.bottom,area.top,area.bottom-menuH);
        D2D1_RECT_F menu={x+w-menuW,my,x+w,my+menuH};app.frontmatterMenuRect=menu;
        fill(app,{menu.left-3*s,menu.top-2*s,menu.right+3*s,menu.bottom+4*s},D2D1::ColorF(0,0,0,.15f),7*s);
        fill(app,menu,app.theme.background,6*s);
        for(size_t i=0;i<opts.size();++i)button(app,{menu.left+5*s,my+6*s+i*29*s,menu.right-5*s,my+31*s+i*29*s},tr(app,formatKey(opts[i])),FM_PICK+(int)opts[i],r.format==opts[i]);
        if(r.hint==fm::Kind::List) {
            float ly=menu.bottom-38*s;
            text(app,tr(app,"fm.up_to"),{menu.left+10*s,ly,menu.right-90*s,ly+28*s},11*s,muted);
            button(app,{menu.right-85*s,ly,menu.right-61*s,ly+26*s},L"-",FM_LIMIT_DOWN);
            text(app,r.limit?std::to_wstring(r.limit):L"All",{menu.right-59*s,ly,menu.right-32*s,ly+26*s},11*s,normal);
            button(app,{menu.right-29*s,ly,menu.right-5*s,ly+26*s},L"+",FM_LIMIT_UP);
        }
    }
}

bool frontmatterAction(App& app,int action) {
    if(action<FM_HIDDEN)return false;
    bool changed=false;
    if(action==FM_HIDDEN||action==FM_SHOWN){app.frontmatter.shown=action==FM_SHOWN;changed=true;app.frontmatterMenu=-1;}
    else if(action==FM_OTHER){app.frontmatter.showOther=!app.frontmatter.showOther;changed=true;}
    else if(action>=FM_PICK&&action<=FM_LIMIT_UP&&app.frontmatterMenu>=0) {
        auto&r=app.frontmatter.rules[app.frontmatterMenu];
        if(action==FM_LIMIT_DOWN)r.limit=std::max(0,r.limit-1);
        else if(action==FM_LIMIT_UP)r.limit=std::min(100,r.limit+1);
        else if(action-FM_PICK<8){r.format=(fm::Format)(action-FM_PICK);app.frontmatterMenu=-1;}
        changed=true;
    } else if(action>=FM_ROW) {
        int row=(action-FM_ROW)/8,col=(action-FM_ROW)%8;
        if(row>=(int)app.frontmatter.rules.size())return true;
        auto&r=app.frontmatter.rules[row];
        if(col==1){r.show=!r.show;changed=true;}
        if(col==2||col==3){r.right=col==3;changed=true;}
        if(col==4){r.label=!r.label;changed=true;}
        if(col==5)app.frontmatterMenu=app.frontmatterMenu==row?-1:row;else app.frontmatterMenu=-1;
    } else return false;
    if(changed){persistFrontmatter(app);app.layoutDirty=true;}
    InvalidateRect(app.hwnd,nullptr,FALSE);return true;
}
bool frontmatterMouseDown(App& app,float x,float y) {
    if(app.settingsSection!=3)return false;
    if(app.frontmatterMenu>=0) {
        if(hit(app.frontmatterMenuRect,x,y))return true;
        app.frontmatterMenu=-1;InvalidateRect(app.hwnd,nullptr,FALSE);
        app.swallowNextMouseUp=true;return true;
    }
    for(const auto&[r,id]:app.settingsHits)if(id>=FM_ROW&&(id-FM_ROW)%8==0&&hit(r,x,y)) {
        app.frontmatterDrag=(id-FM_ROW)/8;app.frontmatterDrop=app.frontmatterDrag;
        SetCapture(app.hwnd);return true;
    }
    return false;
}
void frontmatterMouseMove(App& app,float,float y) {
    if(app.frontmatterDrag<0)return;
    float s=app.frontmatterUiScale;
    if(y<app.frontmatterListRect.top+10*s)app.frontmatterScroll=std::max(0.f,app.frontmatterScroll-7*s);
    if(y>app.frontmatterListRect.bottom-10*s)app.frontmatterScroll+=7*s;
    app.frontmatterDrop=std::clamp((int)std::floor((y-app.frontmatterListRect.top+app.frontmatterScroll)/(37*s)+.5f),0,(int)app.frontmatter.rules.size());
    InvalidateRect(app.hwnd,nullptr,FALSE);
}
bool frontmatterMouseUp(App& app,float x,float y) {
    if(app.frontmatterDrag<0)return false;
    int from=app.frontmatterDrag,to=app.frontmatterDrop;
    if(hit(app.frontmatterListRect,x,y)&&to>=0&&to<=(int)app.frontmatter.rules.size()) {
        if(to>from)--to;
        if(to!=from){auto rule=app.frontmatter.rules[from];app.frontmatter.rules.erase(app.frontmatter.rules.begin()+from);app.frontmatter.rules.insert(app.frontmatter.rules.begin()+to,std::move(rule));persistFrontmatter(app);app.layoutDirty=true;}
    }
    app.frontmatterDrag=app.frontmatterDrop=-1;ReleaseCapture();InvalidateRect(app.hwnd,nullptr,FALSE);return true;
}
void frontmatterScroll(App& app,float delta) {
    if(app.settingsSection!=3)return;
    app.frontmatterMenu=-1;app.frontmatterScroll=std::max(0.f,app.frontmatterScroll-delta*app.frontmatterUiScale*65);
    InvalidateRect(app.hwnd,nullptr,FALSE);
}
void renderFrontmatterOverflow(App& app) {
    if(app.showSettings||app.showContextMenu||app.showThemeChooser||app.showThemeEditor)return;
    float dx=app.mouseX-documentViewportX(app)+app.scrollX,dy=app.mouseY+app.scrollY;
    for(const auto&o:app.frontmatterOverflow)if(hit(o.rect,dx,dy)) {
        float s=app.contentScale,w=std::min(360*s,documentViewportWidth(app)-24*s);
        float x=std::clamp(o.rect.left-app.scrollX,12*s,std::max(12*s,documentViewportWidth(app)-w-12*s));
        float y=o.rect.bottom-app.scrollY+6*s;
        IDWriteTextLayout* layout=nullptr;app.dwriteFactory->CreateTextLayout(o.text.c_str(),(UINT32)o.text.size(),app.folderBrowserFormat,w-20*s,10000,&layout);
        if(!layout)return;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);DWRITE_TEXT_METRICS m{};layout->GetMetrics(&m);
        fill(app,{x,y,x+w,y+m.height+16*s},app.theme.codeBackground,6*s);
        app.brush->SetColor(app.theme.text);app.renderTarget->DrawTextLayout({x+10*s,y+8*s},layout,app.brush);layout->Release();return;
    }
}
