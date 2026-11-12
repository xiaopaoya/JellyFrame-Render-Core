#include "core_text_paragraph.h"
#include "inline_fragment_lines_reader.h"
#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>

namespace jellyframe::detail {
namespace {
using S=TextSystemStatus;
using Kind=CoreTextParagraphSpanKind;
bool fits(std::int64_t n) { return n>=std::numeric_limits<std::int32_t>::min()&&n<=std::numeric_limits<std::int32_t>::max(); }
bool equal_font(const LayoutTextFont& a,const LayoutTextFont& b) {
    return a.family==b.family&&a.size==b.size&&a.weight==b.weight&&a.style==b.style&&a.letters==b.letters&&a.words==b.words;
}
bool whitespace(unsigned char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'; }
bool normal_after(char c,bool break_all) { return break_all||c=='-'||c=='/'; }
bool valid_whitespace(CoreTextWhiteSpace mode) {return mode==CoreTextWhiteSpace::Collapse||mode==CoreTextWhiteSpace::PreserveLines||mode==CoreTextWhiteSpace::Preserve||mode==CoreTextWhiteSpace::PreserveSpaces;}
int pixel(std::int32_t n) { return static_cast<int>((static_cast<std::int64_t>(n)+500)/1000); }
bool valid_options(const CoreTextParagraphOptions& o) {
    return o.width_units>=0&&o.line_height>=-1&&
        valid_whitespace(o.whitespace)&&
        (o.align==TextAlign::Start||o.align==TextAlign::Center||o.align==TextAlign::End)&&
        (o.align_last==TextAlignLast::Auto||o.align_last==TextAlignLast::Start||o.align_last==TextAlignLast::Center||o.align_last==TextAlignLast::End);
}
struct Failure { S status; };
struct Builder {
    const TextSystem& system;
    const CoreTextParagraphSpan* spans;
    const CoreTextLayoutLimits& limits;
    CoreTextParagraphUsage& usage;
    std::size_t capacity_limit;
    std::size_t work_limit;
    CoreTextParagraph draft;
    std::vector<InlineMeasuredPiece> pieces;
    std::optional<std::string> scratch;
    const CoreTextAtomicWidths* atomics = nullptr;
    const CoreTextAtomicGeometry* geometry = nullptr;
    std::size_t atomic_count = 0;
    std::optional<CoreTextParagraphAtomics> placements;
    const CoreTextPreservedOptions* preserved_options = nullptr;
    bool preserved = false, hanging = false, tabs_ready = false;
    std::int32_t tab_interval = 0, tab_half_ch_twice = 0;
    Builder(const TextSystem& s,const CoreTextParagraphSpan* p,const CoreTextLayoutLimits& l,CoreTextParagraphUsage& u)
        : system(s),spans(p),limits(l),usage(u),capacity_limit(l.max_capacity_bytes),work_limit(l.max_line_work),pieces(0) {}
    void work(std::size_t n) {
        if(n>work_limit-std::min(usage.work,work_limit))throw Failure{S::ResourceLimit};
        usage.work+=n;
    }
    void capacity(std::size_t count,std::size_t size) {
        if(count>(capacity_limit-std::min(usage.capacity,capacity_limit))/size)throw Failure{S::ResourceLimit};
        usage.capacity+=count*size;
    }
    void query(std::size_t bytes,std::size_t count=1) {
        if(count>limits.max_queries-std::min(usage.queries,limits.max_queries)||
           bytes>(limits.max_query_bytes-std::min(usage.query_bytes,limits.max_query_bytes))/count)throw Failure{S::ResourceLimit};
        usage.queries+=count;usage.query_bytes+=bytes*count;work(bytes*count);
    }
    TextRunMetrics measure(std::string_view text,const LayoutTextFont& font) {
        if (font.letters || font.words) throw Failure{S::UnsupportedStyle};
        query(text.size());TextRunMetrics m;const auto result=system.measure(text,font.request(),m);
        if(result!=S::Ok) throw Failure{result};
        return m;
    }
    void normalize(std::size_t count,bool intrinsic=false,CoreTextWhiteSpace whitespace_mode=CoreTextWhiteSpace::Collapse,bool preserved_paragraph=false) {
        preserved=whitespace_mode==CoreTextWhiteSpace::Preserve||whitespace_mode==CoreTextWhiteSpace::PreserveSpaces;
        if(preserved&&!intrinsic&&!preserved_paragraph)throw Failure{S::UnsupportedText};
        const bool lines=whitespace_mode==CoreTextWhiteSpace::PreserveLines||preserved;
        const bool converted=whitespace_mode==CoreTextWhiteSpace::PreserveSpaces;
        const auto glyph_limit=std::min(limits.max_prepared_glyphs,kMaxPreparedTextGlyphs);
        if(count>glyph_limit)throw Failure{S::ResourceLimit};
        std::size_t bytes=0,events=0,atomic=0;
        if((atomic_count&&!atomics&&!geometry)||atomic_count>count)throw Failure{S::InvalidRequest};
        for(std::size_t i=0;i<atomic_count;++i) {
            work(1);
            const auto owner=geometry?geometry[i].owner:atomics[i].owner;
            const auto previous=i?(geometry?geometry[i-1].owner:atomics[i-1].owner):0;
            const auto minimum=geometry?geometry[i].advance:atomics[i].min_content;
            const auto maximum=geometry?minimum:atomics[i].max_content;
            if(owner>=count||(i&&owner<=previous)||spans[owner].kind!=Kind::Atomic||minimum<0||maximum<minimum||
                (geometry&&geometry[i].extent<0))throw Failure{S::InvalidRequest};
            if(!fits(maximum)||(geometry&&!fits(static_cast<std::int64_t>(geometry[i].extent)*1000)))throw Failure{S::Overflow};
        }
        for(std::size_t i=0;i<count;++i){work(1);if(spans[i].line_height< -1)throw Failure{S::InvalidRequest};
            if(spans[i].kind!=Kind::Text&&spans[i].kind!=Kind::ForcedBreak&&spans[i].kind!=Kind::Opportunity&&spans[i].kind!=Kind::Atomic)throw Failure{S::InvalidRequest};
            if(spans[i].kind==Kind::Atomic) {
                if(lines)throw Failure{S::UnsupportedText};
                if(!intrinsic&&!geometry)throw Failure{S::UnsupportedText};
                if(atomic==atomic_count||(geometry?geometry[atomic].owner:atomics[atomic].owner)!=i)throw Failure{S::InvalidRequest};
                ++atomic;
            }
            if(spans[i].kind!=Kind::Text){if(!spans[i].source.empty())throw Failure{S::InvalidRequest};++events;}
            if(!spans[i].source.empty()&&!spans[i].source.data())throw Failure{S::InvalidRequest};
            if(spans[i].source.size()>limits.max_source_bytes-bytes) throw Failure{S::ResourceLimit};
            bytes+=spans[i].source.size();
        }
        const auto text_maximum=std::min(bytes,glyph_limit);
        const auto maximum=text_maximum+std::min(events,glyph_limit-text_maximum);
        capacity(std::max<std::size_t>(15,maximum)+1,1);draft.normalized.reserve(maximum);
        if(draft.normalized.capacity()>std::max<std::size_t>(15,maximum))
            capacity(draft.normalized.capacity()-std::max<std::size_t>(15,maximum),1);
        capacity(maximum,sizeof(CoreTextParagraphSource));draft.sources.reserve(maximum);
        bool collapsed=true,after_cr=false;
        for(std::size_t owner=0;owner<count;++owner) {
            const auto kind=spans[owner].kind;
            if(kind!=Kind::Text) {
                after_cr=false;
                work(1);if(kind==Kind::ForcedBreak&&!preserved)drop_trailing_space();
                if(draft.sources.size()>=maximum)throw Failure{S::ResourceLimit};
                draft.normalized.push_back(kind==Kind::ForcedBreak?'\n':kind==Kind::Atomic?'\b':'\v');draft.sources.push_back({owner,0,0});
                if(kind==Kind::ForcedBreak)collapsed=true;
                else if(kind==Kind::Atomic)collapsed=false;
                continue;
            }
            for(std::size_t i=0;i<spans[owner].source.size();++i) {
                work(1);const auto c=static_cast<unsigned char>(spans[owner].source[i]);
                if((c<0x20&&!whitespace(c))||c>=0x7f)throw Failure{S::UnsupportedText};
                if(preserved&&c=='\f')throw Failure{S::UnsupportedText};
                if(lines&&(c=='\r'||c=='\n')) {
                    if(c=='\n'&&after_cr) {
                        after_cr=false;
                        // A cross-owner CRLF belongs to the original CR source;
                        // a same-owner pair retains its complete two-byte range.
                        auto& source=draft.sources.back();
                        if(source.owner==owner&&source.end==i)source.end=i+1;
                        continue;
                    }
                    if(!preserved)drop_trailing_space();
                    if(draft.sources.size()>=maximum)throw Failure{S::ResourceLimit};
                    draft.normalized.push_back(converted?' ':'\n');draft.sources.push_back({owner,i,i+1});
                    collapsed=true;after_cr=c=='\r';continue;
                }
                after_cr=false;
                if(!preserved&&whitespace(c)&&collapsed)continue;
                if(draft.sources.size()>=maximum)throw Failure{S::ResourceLimit};
                collapsed=whitespace(c);draft.normalized.push_back(preserved?(converted&&c=='\t'?' ':static_cast<char>(c)):collapsed?' ':static_cast<char>(c));draft.sources.push_back({owner,i,i+1});
            }
        }
        if(!preserved)drop_trailing_space();
    }
    void drop_trailing_space() {
        // Optional opportunities are transparent to edge whitespace collapse.
        // A forced boundary removes its preceding collapsed separator before
        // emergency search, so it cannot produce a second empty forced line.
        auto end=draft.sources.size();
        while(end&&kind_at(end-1)==Kind::Opportunity){work(1);--end;}
        if(end&&draft.normalized[end-1]==' ') {
            work(draft.sources.size()-end+1);
            draft.normalized.erase(end-1,1);draft.sources.erase(draft.sources.begin()+end-1);
        }
    }
    Kind kind_at(std::size_t at) const {
        return draft.normalized[at]=='\n'?Kind::ForcedBreak:spans[draft.sources[at].owner].kind;
    }

    std::pair<int,int> strut(const TextRunMetrics& m,int height) {
        const auto ascent = pixel(m.ascent), descent = pixel(m.descent);
        const auto selected = height < 0 ? pixel(m.natural_height) : height;
        const auto leading = static_cast<std::int64_t>(selected)-ascent-descent;
        const auto first_half = leading/2-(leading < 0 && leading%2 != 0 ? 1 : 0);
        const auto before = (ascent+first_half)*1000;
        const auto after = static_cast<std::int64_t>(selected)*1000-before;
        if(!fits(before)||!fits(after))throw Failure{S::Overflow};
        return {static_cast<int>(before),static_cast<int>(after)};
    }
    const LayoutTextFont& font_at(std::size_t at) const { return spans[draft.sources[at].owner].font; }
    void trim(std::size_t& first,std::size_t& end,bool maximum=false) {
        const auto empty=[&](std::size_t at,bool tail){return ((!preserved||(tail&&hanging&&!maximum))&&
            (draft.normalized[at]==' '||draft.normalized[at]=='\t'))||(kind_at(at)!=Kind::Text&&kind_at(at)!=Kind::Atomic);};
        while(first<end&&empty(first,false)){work(1);++first;}
        while(end>first&&empty(end-1,true)){work(1);--end;}
    }
    std::size_t group_end(std::size_t at,std::size_t end,bool& gaps) {
        const auto& font=font_at(at);auto next=at+1;gaps=false;
        while(next<end){work(1);
            if(kind_at(next)==Kind::ForcedBreak||kind_at(next)==Kind::Atomic||(preserved&&draft.normalized[next]=='\t'))break;
            if(kind_at(next)==Kind::Opportunity)gaps=true;
            else if(!equal_font(font,font_at(next)))break;
            ++next;
        }
        return next;
    }
    std::string_view run_text(std::size_t first,std::size_t end,bool gaps) {
        if(!gaps)return std::string_view(draft.normalized).substr(first,end-first);
        const auto maximum=end-first;
        if(!scratch){capacity(16,1);scratch.emplace(0,'\0');}
        if(maximum>scratch->capacity()) {
            const auto desired=std::max<std::size_t>(15,maximum)+1;
            // Every new reservation is an acquisition. Earlier scratch storage
            // remains charged even after reserve releases its allocation.
            capacity(desired,1);scratch->reserve(maximum);
            if(scratch->capacity()+1>desired)capacity(scratch->capacity()+1-desired,1);
        }
        scratch->clear();
        for(auto at=first;at<end;++at){work(1);if(kind_at(at)==Kind::Text)scratch->push_back(draft.normalized[at]);}
        return *scratch;
    }
    std::size_t atomic_index(std::size_t owner) {
        std::size_t first=0,last=atomic_count;
        while(first<last){work(1);const auto middle=first+(last-first)/2;
            if((geometry?geometry[middle].owner:atomics[middle].owner)<owner)first=middle+1;else last=middle;}
        if(first==atomic_count||(geometry?geometry[first].owner:atomics[first].owner)!=owner)throw Failure{S::InvalidRequest};
        return first;
    }
    std::int64_t atomic_width(std::size_t owner,bool maximum) {
        const auto at=atomic_index(owner);
        return geometry?geometry[at].advance:maximum?atomics[at].max_content:atomics[at].min_content;
    }
    std::int64_t tab_advance(std::int64_t pen) {
        if(!tabs_ready) {
            tabs_ready=true;
            if(preserved_options->tab_size) {
                auto raw=preserved_options->tab_font;raw.letters=raw.words=0;
                const auto space=measure(" ",raw).advance;
                const auto interval=std::max<std::int64_t>(0,static_cast<std::int64_t>(space)+preserved_options->tab_font.letters+preserved_options->tab_font.words)*preserved_options->tab_size;
                if(!fits(interval)) throw Failure{S::Overflow};
                tab_interval=static_cast<std::int32_t>(interval);
                if(interval)tab_half_ch_twice=measure("0",raw).advance;
            }
        }
        if(!tab_interval)return 0;
        auto step=tab_interval-(pen%tab_interval+tab_interval)%tab_interval;
        if(step*2<tab_half_ch_twice)step+=tab_interval;
        return step;
    }
    void preserved_requests(std::size_t count,const LayoutTextFont& strut) {
        bool mixed=false,spaced=strut.letters||strut.words;
        for(std::size_t i=0;i<count;++i) {
            work(1);mixed|=!equal_font(spans[i].font,strut);
            spaced|=spans[i].font.letters||spans[i].font.words;
            if(!i||!equal_font(spans[i-1].font,spans[i].font))measure({},spans[i].font);
        }
        if(mixed&&spaced)throw Failure{S::UnsupportedText};
    }
    std::int32_t advance(std::size_t first,std::size_t end,bool maximum=false,std::int32_t origin=0) {
        trim(first,end,maximum);std::int64_t pen=origin;
        bool previous=false,previous_atomic=false;std::int32_t letters=0;
        for(auto at=first;at<end;) {
            if(preserved&&draft.normalized[at]=='\t') {
                work(1);
                pen+=tab_advance(pen);if(!fits(pen))throw Failure{S::Overflow};
                previous=previous_atomic=false;++at;continue;
            }
            if((atomic_count||preserved)&&kind_at(at)==Kind::Opportunity){work(1);++at;continue;}
            if(atomic_count&&kind_at(at)==Kind::Atomic) {
                work(1);const auto next_letters=font_at(at).letters;
                if(previous&&!previous_atomic)pen+=(static_cast<std::int64_t>(letters)+next_letters)/2;
                if(!fits(pen))throw Failure{S::Overflow};
                pen+=atomic_width(draft.sources[at].owner,maximum);
                if(!fits(pen))throw Failure{S::Overflow};
                previous=previous_atomic=true;letters=next_letters;++at;continue;
            }
            if(kind_at(at)!=Kind::Text)throw Failure{S::InvalidRequest};
            bool gaps=false;const auto next=group_end(at,end,gaps);
            if(previous_atomic)pen+=(static_cast<std::int64_t>(letters)+font_at(at).letters)/2;
            if(!fits(pen))throw Failure{S::Overflow};
            pen+=measure(run_text(at,next,gaps),font_at(at)).advance;
            previous=true;previous_atomic=false;letters=font_at(at).letters;
            if(!fits(pen)) throw Failure{S::Overflow};
            at=next;
        }
        const auto result=std::max<std::int64_t>(0,pen-origin);
        if(!fits(result))throw Failure{S::Overflow};
        return static_cast<std::int32_t>(result);
    }
    void plan(const CoreTextParagraphOptions& options,InlineLinePlan& plan) {
        const auto width=options.width_units;
        const auto indent=std::clamp(static_cast<std::int64_t>(options.indent)*1000,-width,width);
        if(!fits(width)||!fits(indent))throw Failure{S::Overflow};
        const auto base_strut=strut(measure({},options.strut),options.line_height);
        capacity(draft.sources.size(),sizeof(InlineMeasuredPiece));pieces.reserve(draft.sources.size());
        for(std::size_t i=0;i<draft.sources.size();++i) {
            work(1);const auto owner=draft.sources[i].owner;const auto& span=spans[owner];
            const auto kind=kind_at(i);
            const auto vertical=kind==Kind::Atomic?std::pair<int,int>{geometry[atomic_index(owner)].extent*1000,0}:kind==Kind::Opportunity?base_strut:
                strut(measure(kind==Kind::ForcedBreak||(preserved&&draft.normalized[i]=='\t')?std::string_view{}:std::string_view(draft.normalized).substr(i,1),span.font),span.line_height);
            InlineMeasuredPiece piece;
            piece.owner=owner;piece.before=vertical.first;piece.after=vertical.second;
            piece.own_before=vertical.first;piece.own_after=vertical.second;
            piece.kind=kind==Kind::Atomic?InlinePieceKind::Atomic:kind==Kind::ForcedBreak?InlinePieceKind::ForcedBreak:kind==Kind::Opportunity?InlinePieceKind::Opportunity:
                draft.normalized[i]==' '?InlinePieceKind::CollapsibleSpace:InlinePieceKind::Text;
            piece.normal_before=kind==Kind::Atomic;
            piece.normal_after=kind==Kind::Atomic||(kind==Kind::Text&&normal_after(draft.normalized[i],options.break_all));
            piece.emergency_before=kind==Kind::Text&&options.anywhere;piece.soft_wrap_allowed=!options.nowrap;pieces.push_back(piece);
            if(preserved&&kind==Kind::Text) {
                auto& p=pieces.back();const auto c=draft.normalized[i];const bool space=c==' '||c=='\t';
                p.kind=c=='\t'?InlinePieceKind::Tab:c==' '?InlinePieceKind::PreservedSpace:InlinePieceKind::Text;
                p.hang_at_end=space&&hanging;
                auto next=i+1;
                if(space&&hanging)while(next<draft.sources.size()&&kind_at(next)==Kind::Opportunity){work(1);++next;}
                const bool last_space=space&&(next==draft.sources.size()||kind_at(next)!=Kind::Text||
                    (draft.normalized[next]!=' '&&draft.normalized[next]!='\t'));
                p.normal_before=options.break_all&&(!space||!hanging);
                p.normal_after=c=='-'||c=='/'||(space&&(!hanging||last_space));
                p.emergency_before=options.anywhere&&(!space||!hanging);
            }
        }
        InlineLinePlanOptions plan_options;plan_options.width=static_cast<int>(width);plan_options.first_line_indent=static_cast<int>(indent);
        plan_options.strut_before=base_strut.first;plan_options.strut_after=base_strut.second;plan_options.height_only=true;plan_options.compact_line_storage=true;
        plan_options.range.context=this;
        plan_options.range.usage=[](void* data){const auto& builder=*static_cast<Builder*>(data);return InlineLineRangeUsage{builder.usage.work,builder.usage.capacity};};
        plan_options.range.advance=[](std::size_t first,std::size_t end,int origin,const InlineLineRangeBudget& budget,int* advance,void* data) {
            auto& builder=*static_cast<Builder*>(data);const auto before=builder.usage.work;
            struct RangeBudgetScope {
                Builder& builder;
                std::size_t previous;
                std::size_t previous_work;
                ~RangeBudgetScope() {
                    builder.capacity_limit=previous;
                    builder.work_limit=previous_work;
                }
            } scope{builder,builder.capacity_limit,builder.work_limit};
            builder.capacity_limit=std::min(builder.capacity_limit,builder.usage.capacity+budget.capacity);
            builder.work_limit=std::min(builder.work_limit,builder.usage.work+budget.work);
            *advance=builder.advance(first,end,false,builder.preserved?origin:0);return builder.usage.work-before<=budget.work;
        };
        InlineLinePlanLimits plan_limits;plan_limits.max_input_items=limits.max_prepared_glyphs;plan_limits.max_lines=limits.max_lines;
        plan_limits.max_work=limits.max_line_work;plan_limits.max_capacity_bytes=limits.max_capacity_bytes;
        std::size_t planner_work=0;
        std::size_t planner_capacity=0;
        struct PlannerUsageReport {
            CoreTextParagraphUsage& usage;
            const std::size_t& work;
            const std::size_t& capacity;
            ~PlannerUsageReport() {
                usage.work+=std::min(work,std::numeric_limits<std::size_t>::max()-usage.work);
                usage.capacity+=std::min(capacity,std::numeric_limits<std::size_t>::max()-usage.capacity);
            }
        } planner_usage{usage,planner_work,planner_capacity};
        const auto planned=plan_inline_fragment_lines(pieces.data(),pieces.size(),plan_options,plan,plan_limits,&planner_work,&planner_capacity);
        if(planner_work>limits.max_line_work-std::min(usage.work,limits.max_line_work)||
            planner_capacity>limits.max_capacity_bytes-std::min(usage.capacity,limits.max_capacity_bytes))
            throw Failure{S::ResourceLimit};
        if(planned!=InlineLinePlanStatus::Complete)throw Failure{planned==InlineLinePlanStatus::CoordinateLimit?S::Overflow:S::ResourceLimit};
    }
    void emit(std::size_t row,std::size_t first,std::size_t end,std::int32_t origin,std::int64_t alignment=0) {
        std::int64_t pen=origin;
        bool previous_text=false,previous_atomic=false;std::int32_t letters=0;
        for(auto at=first;at<end;) {
            if((atomic_count||preserved)&&kind_at(at)==Kind::Opportunity){work(1);++at;continue;}
            if(preserved&&draft.normalized[at]=='\t') {
                // Center/end alignment moves the finished line, never its
                // containing-block Tab grid. No Tab record enters a packet.
                work(1);pen+=tab_advance(pen-alignment);
                if(!fits(pen))throw Failure{S::Overflow};
                previous_text=previous_atomic=false;++at;continue;
            }
            if(atomic_count&&kind_at(at)==Kind::Atomic) {
                work(1);const auto owner=draft.sources[at].owner;const auto& value=geometry[atomic_index(owner)];
                const auto next_letters=font_at(at).letters;
                if(previous_text)pen+=(static_cast<std::int64_t>(letters)+next_letters)/2;
                const auto top=static_cast<std::int64_t>(draft.lines[row].baseline)-value.extent;
                if(!fits(pen)||!fits(pen+value.advance)||!fits(top))throw Failure{S::Overflow};
                placements->placements.push_back({owner,row,static_cast<std::int32_t>(pen),static_cast<std::int32_t>(value.advance),static_cast<int>(top),value.extent});
                pen+=value.advance;previous_text=false;previous_atomic=true;letters=next_letters;++at;continue;
            }
            bool gaps=false;const auto next=group_end(at,end,gaps);const auto& font=font_at(at);
            if(previous_atomic)pen+=(static_cast<std::int64_t>(letters)+font.letters)/2;
            if(!fits(pen))throw Failure{S::Overflow};
            const auto text=run_text(at,next,gaps);
            capacity(text.size(),sizeof(PreparedTextGlyph));std::vector<PreparedTextGlyph> glyphs(text.size());
            query(text.size(),2);TextRunMetrics m;
            const auto result=system.prepare(text,font.request(),glyphs.data(),glyphs.size(),m);
            if(result!=S::Ok)throw Failure{result};
            std::size_t source_at=at;
            for(std::size_t begin=0;begin<glyphs.size();) {
                while(source_at<next&&kind_at(source_at)==Kind::Opportunity){work(1);++source_at;}
                const auto owner=draft.sources[source_at].owner;auto source_end=source_at+1;
                while(source_end<next&&draft.sources[source_end].owner==owner){work(1);++source_end;}
                const auto stop=begin+source_end-source_at;
                const auto packet_origin=pen+glyphs[begin].x;
                if(!fits(packet_origin))throw Failure{S::Overflow};
                for(auto i=begin;i<stop;++i) {
                    work(1);auto& g=glyphs[i];const auto x=pen+g.x;
                    const auto glyph_end=x+std::max<std::int64_t>(0,static_cast<std::int64_t>(g.geometry.advance)+(g.source_codepoint==' '?font.words:0));
                    if(!fits(x)||!fits(glyph_end)) throw Failure{S::Overflow};
                    g.x=static_cast<std::int32_t>(x);
                    g.source_begin-=static_cast<std::uint32_t>(begin);g.source_end-=static_cast<std::uint32_t>(begin);
                }
                capacity(kPreparedTextHeaderBytes+(stop-begin)*kPreparedTextRecordBytes+1,1);
                draft.fragments.emplace_back();auto& fragment=draft.fragments.back();
                fragment.owner=owner;fragment.line=row;fragment.begin=source_at;fragment.end=source_end;
                if(!encode_prepared_text(glyphs.data()+begin,stop-begin,font.size,static_cast<std::uint32_t>(stop-begin),
                    fragment.packet,static_cast<std::int32_t>(packet_origin)))throw Failure{S::ResourceLimit};
                begin=stop;source_at=source_end;
            }
            pen+=m.advance;if(!fits(pen))throw Failure{S::Overflow};at=next;
            previous_text=true;previous_atomic=false;letters=font.letters;
        }
    }
};
}

S build_core_text_paragraph(const TextSystem& system,const CoreTextParagraphSpan* spans,std::size_t count,
    const CoreTextParagraphOptions& options,CoreTextParagraph& output,CoreTextLayoutLimits limits,CoreTextParagraphUsage* used,
    const CoreTextAtomicGeometry* geometry,std::size_t atomic_count,CoreTextParagraphAtomics* atomic_output,
    const CoreTextPreservedOptions* preserved_options) noexcept {
    CoreTextParagraphUsage usage;
    struct Report { CoreTextParagraphUsage* out;CoreTextParagraphUsage& value;~Report(){if(out)*out=value;} } report{used,usage};
    try {
        if(!system.ready())return S::NotReady;
        if((count&&!spans)||!valid_options(options)||(atomic_count&&!atomic_output))return S::InvalidRequest;
        const bool preserved=options.whitespace==CoreTextWhiteSpace::Preserve||options.whitespace==CoreTextWhiteSpace::PreserveSpaces;
        if(!preserved&&preserved_options)return S::InvalidRequest;
        const auto glyph_limit=std::min(limits.max_prepared_glyphs,kMaxPreparedTextGlyphs);
        if(count>glyph_limit)return S::ResourceLimit;
        Builder b{system,spans,limits,usage};b.geometry=geometry;b.atomic_count=atomic_count;
        b.preserved_options=preserved_options;b.hanging=preserved_options&&!options.nowrap&&!preserved_options->break_spaces;
        b.draft.revision=system.generation();b.normalize(count,false,options.whitespace,preserved_options!=nullptr);
        if(preserved)b.preserved_requests(count,options.strut);
        if(atomic_count) {
            b.capacity(atomic_count,sizeof(CoreTextAtomicPlacement));b.placements.emplace();b.placements->placements.reserve(atomic_count);
            if(b.placements->placements.capacity()>atomic_count)b.capacity(b.placements->placements.capacity()-atomic_count,sizeof(CoreTextAtomicPlacement));
        }
        InlineLinePlan plan;b.plan(options,plan);
        const auto indent=std::clamp(static_cast<std::int64_t>(options.indent)*1000,-options.width_units,options.width_units);
        b.capacity(plan.lines.size(),sizeof(CoreTextParagraphLine));b.draft.lines.reserve(plan.lines.size());
        b.capacity(b.draft.sources.size(),sizeof(CoreTextParagraphFragment));b.draft.fragments.reserve(b.draft.sources.size());
        for(std::size_t row=0;row<plan.lines.size();++row) {
            b.work(1);const auto& line=plan.lines[row];auto first=line.input_begin,end=line.input_end;b.trim(first,end,preserved);
            const auto initial=row?0:static_cast<int>(indent);
            const bool soft=line.end==InlineLineEnd::SoftWrap;
            const auto advance=b.advance(first,end,preserved,preserved?initial:0);
            const auto aligned=preserved&&b.hanging&&(soft||static_cast<std::int64_t>(advance)+initial>options.width_units)?
                b.advance(first,end,false,initial):advance;
            const auto spare=std::max<std::int64_t>(0,options.width_units-initial-aligned);
            auto align=options.align;
            if(!soft)switch(options.align_last) {case TextAlignLast::Start:align=TextAlign::Start;break;case TextAlignLast::Center:align=TextAlign::Center;break;case TextAlignLast::End:align=TextAlign::End;break;default:break;}
            const auto origin=initial+(align==TextAlign::Center?spare/2:align==TextAlign::End?spare:0);
            if(!fits(origin))return S::Overflow;
            b.draft.lines.push_back({first,end,line.rect.y/1000,line.rect.height/1000,line.baseline/1000,static_cast<std::int32_t>(origin),advance,soft});
            b.emit(row,first,end,static_cast<std::int32_t>(origin),preserved?origin-initial:0);
        }
        b.draft.height=plan.height/1000;
        if(system.generation()!=b.draft.revision)return S::InvalidRequest;
        if(atomic_output) {
            if(b.placements)atomic_output->placements=std::move(b.placements->placements);
            else atomic_output->placements.clear();
        }
        output=std::move(b.draft);return S::Ok;
    } catch(const Failure& failure){return failure.status;}
      catch(const std::bad_alloc&){return S::ResourceLimit;}
      catch(const std::length_error&){return S::ResourceLimit;}
}
S measure_core_text_paragraph_metrics(const TextSystem& system,const CoreTextParagraphSpan* spans,std::size_t count,
    const CoreTextParagraphOptions& options,CoreTextParagraphMetrics& output,CoreTextLayoutLimits limits,CoreTextParagraphUsage* used,
    const CoreTextAtomicGeometry* geometry,std::size_t atomic_count,const CoreTextPreservedOptions* preserved_options) noexcept {
    CoreTextParagraphUsage usage;
    struct Report {CoreTextParagraphUsage* out;CoreTextParagraphUsage& value;~Report(){if(out)*out=value;}} report{used,usage};
    try {
        if(!system.ready())return S::NotReady;
        if((count&&!spans)||!valid_options(options))return S::InvalidRequest;
        const bool preserved=options.whitespace==CoreTextWhiteSpace::Preserve||options.whitespace==CoreTextWhiteSpace::PreserveSpaces;
        if(preserved_options&&!preserved)return S::InvalidRequest;
        Builder b{system,spans,limits,usage};b.geometry=geometry;b.atomic_count=atomic_count;
        b.preserved_options=preserved_options;b.hanging=preserved_options&&!options.nowrap&&!preserved_options->break_spaces;
        b.draft.revision=system.generation();b.normalize(count,false,options.whitespace,preserved_options!=nullptr);
        if(preserved)b.preserved_requests(count,options.strut);
        InlineLinePlan plan;b.plan(options,plan);
        CoreTextParagraphMetrics draft;draft.height=plan.height/1000;draft.line_count=plan.lines.size();
        if(draft.line_count){draft.first_baseline=plan.lines.front().baseline/1000;draft.last_baseline=plan.lines.back().baseline/1000;}
        if(system.generation()!=b.draft.revision)return S::InvalidRequest;
        output=draft;return S::Ok;
    }catch(const Failure& failure){return failure.status;}
     catch(const std::bad_alloc&){return S::ResourceLimit;}
     catch(const std::length_error&){return S::ResourceLimit;}
}
S measure_core_text_intrinsic(const TextSystem& system,const CoreTextParagraphSpan* spans,std::size_t count,
    const CoreTextIntrinsicOptions& options,CoreTextIntrinsicSizes& output,CoreTextLayoutLimits limits,CoreTextParagraphUsage* used,
    const CoreTextAtomicWidths* atomics,std::size_t atomic_count) noexcept {
    CoreTextParagraphUsage usage;
    struct Report {CoreTextParagraphUsage* out;CoreTextParagraphUsage& value;~Report(){if(out)*out=value;}} report{used,usage};
    try {
        if(!system.ready())return S::NotReady;
        if((count&&!spans)||!valid_whitespace(options.whitespace)||
            (options.preserved.break_spaces&&options.whitespace!=CoreTextWhiteSpace::Preserve&&options.whitespace!=CoreTextWhiteSpace::PreserveSpaces))return S::InvalidRequest;
        Builder b{system,spans,limits,usage};b.atomics=atomics;b.atomic_count=atomic_count;
        b.preserved_options=&options.preserved;b.hanging=!options.nowrap&&!options.preserved.break_spaces;
        b.draft.revision=system.generation();b.normalize(count,true,options.whitespace);
        bool mixed=false,spaced=false,initialized=false;LayoutTextFont first_font;
        for(std::size_t i=0;i<count;++i){b.work(1);if(spans[i].kind!=Kind::Text)continue;
            if(initialized)mixed|=!equal_font(first_font,spans[i].font);else{first_font=spans[i].font;initialized=true;}
            spaced|=spans[i].font.letters||spans[i].font.words;}
        if(mixed&&spaced)return S::UnsupportedText;
        // Validate complete requests even for collapsed/empty/event owners.
        for(std::size_t i=0;i<count;++i)if(spans[i].kind!=Kind::Atomic&&(!i||!equal_font(spans[i-1].font,spans[i].font)||spans[i-1].kind==Kind::Atomic))b.measure({},spans[i].font);
        CoreTextIntrinsicSizes draft;const auto end=b.draft.normalized.size();
        std::size_t natural=0,fragment=0;bool pending_space=false;
        for(std::size_t i=0;i<end;++i) {
            b.work(1);const auto kind=b.kind_at(i);const auto c=b.draft.normalized[i];
            if(kind==Kind::ForcedBreak) {
                pending_space=false;
                draft.max_content=std::max(draft.max_content,b.advance(natural,i,true));natural=i+1;
                draft.min_content=std::max(draft.min_content,b.advance(fragment,i));fragment=i+1;
            }else if(!options.nowrap) {
                if(b.preserved&&kind==Kind::Text&&(c==' '||c=='\t')) {
                    if(options.preserved.break_spaces){draft.min_content=std::max(draft.min_content,b.advance(fragment,i+1));fragment=i+1;}
                    else pending_space=true;
                    continue;
                }
                if(pending_space&&kind==Kind::Text) {
                    draft.min_content=std::max(draft.min_content,b.advance(fragment,i));fragment=i;pending_space=false;
                }
                if(kind==Kind::Atomic) {
                    draft.min_content=std::max(draft.min_content,b.advance(fragment,i));
                    draft.min_content=std::max(draft.min_content,b.advance(i,i+1));fragment=i+1;
                }else if(kind==Kind::Opportunity||c==' ') {draft.min_content=std::max(draft.min_content,b.advance(fragment,i));fragment=i+1;}
                else if(normal_after(c,options.break_all||options.anywhere)) {
                    draft.min_content=std::max(draft.min_content,b.advance(fragment,i+1));fragment=i+1;
                }
            }
        }
        draft.max_content=std::max(draft.max_content,b.advance(natural,end,true));
        draft.min_content=options.nowrap&&!atomic_count?draft.max_content:std::max(draft.min_content,b.advance(fragment,end));
        draft.max_content=std::max(draft.max_content,draft.min_content);
        if(system.generation()!=b.draft.revision)return S::InvalidRequest;
        output=draft;return S::Ok;
    }catch(const Failure& failure){return failure.status;}
      catch(const std::bad_alloc&){return S::ResourceLimit;}
      catch(const std::length_error&){return S::ResourceLimit;}
}
} // namespace jellyframe::detail
