#include "gameplay_ui.h"
#include <algorithm>
#include <cmath>

namespace project::ui {
namespace {
bool decode(std::string_view s,std::size_t& i,char32_t& cp) {
    if(i>=s.size()) return false;
    unsigned char first=static_cast<unsigned char>(s[i++]);
    if(first<128) { cp=first; return true; }
    unsigned count=first>=0xC2&&first<=0xDF?1:first>=0xE0&&first<=0xEF?2:first>=0xF0&&first<=0xF4?3:0;
    if(!count||i+count>s.size()) return false;
    cp=first&((1u<<(6-count))-1);
    for(unsigned j=0;j<count;++j) { unsigned char c=static_cast<unsigned char>(s[i++]); if((c&0xC0)!=0x80) return false; cp=(cp<<6)|(c&63); }
    return !(cp< (count==1?0x80u:count==2?0x800u:0x10000u)||cp>0x10FFFF||(cp>=0xD800&&cp<=0xDFFF));
}
bool validRect(Rect r) { return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0&&r.width<=16384&&r.height<=16384; }
void clampScroll(PanelSnapshot& p) { p.scrollOffset=std::clamp(p.scrollOffset,0.f,std::max(0.f,p.contentHeight-p.rect.height)); }
}
bool validUtf8(std::string_view text) { std::size_t i=0; char32_t cp=0; while(i<text.size()) if(!decode(text,i,cp)) return false; return true; }
UiModel::UiModel(GlyphAdvance advance):m_advance(std::move(advance)) {}
bool UiModel::layout(PanelSnapshot& p) {
    const float rowHeight = p.lineHeight * p.scale;
    const float defaultAdvance = p.glyphAdvance * p.scale;
    if(!std::isfinite(rowHeight) || rowHeight <= 0 || !std::isfinite(defaultAdvance) || defaultAdvance <= 0) return false;
    p.lines.clear(); std::string line; double width=0; std::size_t i=0;
    auto append=[&] { if(p.lines.size()<4096) p.lines.push_back(line); line.clear(); width=0; };
    while(i<p.text.size()&&p.lines.size()<4096) {
        auto begin=i; char32_t cp=0; if(!decode(p.text,i,cp)) break;
        if(cp=='\r') continue;
        if(cp=='\n') { append(); continue; }
        float advance=m_advance?m_advance(p.fontAsset,cp):p.glyphAdvance;
        if(!std::isfinite(advance)||advance<0) advance=p.glyphAdvance;
        const bool positiveAdvance = advance > 0;
        advance*=p.scale;
        if(!std::isfinite(advance) || (positiveAdvance && advance <= 0)) return false;
        if(width+advance>p.rect.width&&!line.empty()) append();
        line.append(p.text.substr(begin,i-begin)); width+=advance;
    }
    if(p.lines.size()<4096) append();
    p.contentHeight=(static_cast<float>(p.lines.size()+p.choices.size()))*rowHeight;
    if(!std::isfinite(p.contentHeight)) return false;
    p.clip=p.rect; clampScroll(p); return i==p.text.size()&&p.lines.size()<4096;
}
void UiModel::capture() { m_suppressed.insert(m_raw.held.begin(),m_raw.held.end()); m_suppressed.insert(m_raw.pressed.begin(),m_raw.pressed.end()); m_transition=true; }
bool UiModel::open(PanelOptions options) {
    if(m_panels.size()>=64||options.id.empty()||options.id.size()>128||options.fontAsset.size()>128||options.text.size()>65536||!validUtf8(options.text)||!validRect(options.rect)||!std::isfinite(options.scale)||options.scale<=0||options.scale>16||!std::isfinite(options.lineHeight)||options.lineHeight<=0||options.lineHeight>1024||!std::isfinite(options.glyphAdvance)||options.glyphAdvance<=0||options.choices.size()>64) return false;
    for(float c:options.color) if(!std::isfinite(c)||c<0||c>1) return false;
    for(const auto& choice:options.choices) if(choice.size()>4096||!validUtf8(choice)) return false;
    if(std::any_of(m_panels.begin(),m_panels.end(),[&](const auto& p){return p.id==options.id;})) return false;
    PanelSnapshot p; static_cast<PanelOptions&>(p)=std::move(options); if(!layout(p)) return false;
    if(!m_panels.empty()) m_panels.back().focused=false;
    p.focused=true; m_panels.push_back(std::move(p)); capture(); return true;
}
bool UiModel::close(std::string_view id) {
    auto i=std::find_if(m_panels.begin(),m_panels.end(),[&](const auto& p){return p.id==id;}); if(i==m_panels.end()) return false;
    capture(); m_panels.erase(i); if(!m_panels.empty()) m_panels.back().focused=true; return true;
}
bool UiModel::setText(std::string_view id,std::string text) {
    if(text.size()>65536||!validUtf8(text)) return false;
    for(auto& p:m_panels) if(p.id==id) { auto copy=p; copy.text=std::move(text); if(!layout(copy)) return false; p=std::move(copy); return true; } return false;
}
bool UiModel::resize(std::string_view id,Rect rect) { if(!validRect(rect)) return false; for(auto& p:m_panels) if(p.id==id) {auto copy=p;copy.rect=rect;if(!layout(copy)) return false;p=std::move(copy);return true;} return false; }
bool UiModel::scroll(std::string_view id,float delta) { if(!std::isfinite(delta)) return false; for(auto& p:m_panels) if(p.id==id) {p.scrollOffset+=delta;clampScroll(p);return true;} return false; }
bool UiModel::modalActive() const { return std::any_of(m_panels.begin(),m_panels.end(),[](const auto& p){return p.modal;}); }
void UiModel::tick(const interaction::ActionFrame& input) {
    m_raw=input; m_transition=false;
    for(auto i=m_suppressed.begin();i!=m_suppressed.end();) { if(!input.held.contains(*i)&&!input.pressed.contains(*i)&&!input.released.contains(*i)) i=m_suppressed.erase(i); else ++i; }
    if(m_panels.empty()||!input.focused) return;
    auto& p=m_panels.back();
    auto pressed=[&](const std::string& name){return input.pressed.contains(name)&&!m_suppressed.contains(name);};
    if(pressed(p.cancelAction)&&p.dismissible) { auto id=p.id; m_events.push_back({id,"cancel",p.selected}); close(id); while(m_events.size()>256) m_events.pop_front(); return; }
    if(!p.choices.empty()) {
        auto before=p.selected;
        if(pressed(p.upAction)) p.selected=(p.selected+p.choices.size()-1)%p.choices.size();
        if(pressed(p.downAction)) p.selected=(p.selected+1)%p.choices.size();
        if(before!=p.selected) m_events.push_back({p.id,"selection",p.selected});
    } else { if(pressed(p.upAction)) scroll(p.id,-p.lineHeight*p.scale); if(pressed(p.downAction)) scroll(p.id,p.lineHeight*p.scale); }
    bool inside=input.pointerX>=p.rect.x&&input.pointerX<p.rect.x+p.rect.width&&input.pointerY>=p.rect.y&&input.pointerY<p.rect.y+p.rect.height;
    if(inside&&input.wheelY!=0) scroll(p.id,-input.wheelY*p.lineHeight*p.scale*3);
    bool click=pressed(p.pointerAction)&&inside;
    if(click&&!p.choices.empty()) {
        const double rowHeight = static_cast<double>(p.lineHeight * p.scale);
        const double local = static_cast<double>(input.pointerY) - p.rect.y + p.scrollOffset - static_cast<double>(p.lines.size()) * rowHeight;
        const double rowIndex = local / rowHeight;
        // Bound the floating result before conversion; tiny valid rows may yield huge indices.
        if(!std::isfinite(rowIndex) || rowIndex < 0 || rowIndex >= static_cast<double>(p.choices.size())) click=false;
        else { const auto row=static_cast<std::size_t>(rowIndex);p.selected=row;m_events.push_back({p.id,"selection",p.selected}); }
    }
    if(pressed(p.confirmAction)||click) m_events.push_back({p.id,"confirm",p.selected});
    while(m_events.size()>256) m_events.pop_front();
}
interaction::ActionFrame UiModel::gameplayFrame() const {
    if(modalActive()||m_transition) { interaction::ActionFrame empty;empty.focused=m_raw.focused;return empty; }
    auto result=m_raw;
    for(const auto& name:m_suppressed) {result.pressed.erase(name);result.held.erase(name);result.released.erase(name);result.values.erase(name);} return result;
}
std::optional<UiEvent> UiModel::pollEvent() { if(m_events.empty()) return {};auto e=m_events.front();m_events.pop_front();return e; }
void UiModel::clear() { capture();m_panels.clear();m_events.clear(); }
}
