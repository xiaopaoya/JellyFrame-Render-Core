#include "contextual_text_layout_internal.h"
#include "computed_size_geometry.h"
#include <algorithm>
#include <climits>

namespace jellyframe::contextual_text {
const LayoutBox* LayoutResult::tree() const noexcept {return state_?state_->tree.get():nullptr;}
const std::vector<LayoutParagraph>& LayoutResult::paragraphs() const noexcept {
  static const std::vector<LayoutParagraph> empty;
  return state_?state_->paragraphs:empty;
}
std::uint64_t LayoutResult::source_generation() const noexcept {return state_?state_->source_generation:0;}
}
namespace jellyframe::detail {
namespace {
bool zero(EdgeSizes e) {return !e.top&&!e.right&&!e.bottom&&!e.left;}
bool bounded(EdgeSizes e) {
  return e.top>=0&&e.right>=0&&e.bottom>=0&&e.left>=0&&
         e.top<=8192&&e.right<=8192&&e.bottom<=8192&&e.left<=8192;
}
bool ordinary(std::string_view name) {
  for(auto allowed:{"document","#document","html","body","div","p","section","article","main",
      "header","footer","nav","aside","span","a","h1","h2","h3","h4","h5","h6"})
    if(name==allowed)return true;
  return name.empty();
}
bool plain(const Style& s) {
  if(s.visibility_hidden||s.position_type!=PositionType::Static||s.opacity!=1||
     !s.transform.empty()||(!s.overflow.empty()&&s.overflow!="visible")||
     s.box_shadow.enabled||s.text_shadow.enabled||s.outline_width||
     s.before_content_kind!=GeneratedContentKind::None||s.after_content_kind!=GeneratedContentKind::None||
     s.list_style_type!=ListStyleType::None||s.text_decoration_underline||s.text_decoration_line_through||
     s.text_transform!=TextTransform::None||s.letter_spacing||s.word_spacing||s.text_indent||s.text_overflow_ellipsis||
     s.line_height==0||s.line_height>INT_MAX/1000||!s.transitions.empty()||!s.animations.empty()||
     s.aspect_ratio_width||s.aspect_ratio_height||!bounded(s.margin)||!bounded(s.padding)||!zero(s.border_width)||
     s.border_radius||s.border_radius_percent>=0||s.border_radius_axes_specified||
     s.background_layer_count!=1||s.background_layers[0].kind!=BackgroundLayerKind::None||
     !s.z_index_auto||(s.table_properties & kTableUnqualifiedSizingMask))return false;
  if(!s.computed_sizes.empty())return false;
  if(s.height>=0||s.min_height>=0||s.max_height>=0||s.height_percent>=0||
     s.min_height_percent>=0||s.max_height_percent>=0)return false;
  return true;
}
}
contextual_text::Status ContextualLayoutFrame::admit(const RenderObject& root,std::size_t max_boxes,std::size_t max_depth) {
  using contextual_text::Status;
  if(!options.binding.valid())return status=Status::invalid;
  if(!options.max_paragraphs||options.max_paragraphs>128||!options.max_lines||options.max_lines>128||
     !options.max_source_bytes||options.max_source_bytes>65536||!options.max_text_result_bytes||
     options.max_text_result_bytes>8U*1024U*1024U||!max_boxes||!max_depth||
     !options.paragraph.shape_calls||!options.paragraph.shaping.work||work>options.paragraph.shaping.work)
    return status=Status::limit;
  struct Pending {const RenderObject* object;std::size_t depth;};
  std::vector<Pending> pending{{&root,1}};
  while(!pending.empty()) {
    const auto next=pending.back();pending.pop_back();
    if(usage.boxes==max_boxes||next.depth>max_depth||!work)return status=Status::limit;
    ++usage.boxes;--work;
    const auto& o=*next.object;
    if(o.children.size()>max_boxes-usage.boxes||pending.size()>max_boxes-usage.boxes-o.children.size())return status=Status::limit;
    if(!plain(o.style)||!o.node||o.contents_sources)return status=Status::invalid;
    if(o.type==RenderObjectType::Text) {
      const auto q=std::int64_t(o.style.font_size)*1000;
      if(o.node->type!=NodeType::Text||!o.children.empty()||o.style.font_family_hash!=options.font_family_hash||
         o.style.font_weight!=options.binding.weight()||q<=0||q>INT_MAX||
         !zero(o.style.margin)||!zero(o.style.padding)||o.style.background_color.a)return status=Status::invalid;
      if(++expected_paragraphs>options.max_paragraphs||o.node->text.size()>options.max_source_bytes-usage.source_bytes)return status=Status::limit;
      usage.source_bytes+=o.node->text.size();
    } else {
      if((o.type!=RenderObjectType::View&&o.type!=RenderObjectType::Block)||o.style.display!=Display::Block||
         o.node->type!=NodeType::Element||!ordinary(o.node->tag_name))return status=Status::invalid;
      std::size_t texts=0;
      for(const auto& child:o.children)if(child&&child->type==RenderObjectType::Text)++texts;
      if(texts&&(texts!=1||o.children.size()!=1))return status=Status::invalid;
    }
    for(auto i=o.children.rbegin();i!=o.children.rend();++i) {
      if(!*i)return status=Status::invalid;
      pending.push_back({i->get(),next.depth+1});
    }
  }
  draft=std::make_shared<contextual_text::LayoutResult::State>();
  draft->paragraphs.reserve(expected_paragraphs);
  if(draft->paragraphs.capacity()>options.max_text_result_bytes/sizeof(contextual_text::LayoutParagraph))return status=Status::limit;
  usage.text_result_bytes=draft->paragraphs.capacity()*sizeof(contextual_text::LayoutParagraph);
  auto* document=root.node;
  // A bounded parent climb also rejects corrupt/cyclic borrowed parent chains.
  std::size_t ancestors=0;
  while(document&&document->parent) {
    if(++ancestors>max_depth||!work)return status=Status::limit;
    --work;document=document->parent;
  }
  draft->source_root=document;
  draft->source_generation=document?document->mutation_generation:0;
  if (!document || !const_cast<Node*>(document)->add_lifetime_observer(
          draft->source_lifetime, [](Node&, void*) noexcept {}, nullptr)) {
    return status=Status::allocation;
  }
  return status;
}
std::int64_t ContextualLayoutFrame::prepare(LayoutBox& box,std::int64_t x,std::int64_t y,std::int64_t width) {
  using contextual_text::Status;
  if(status!=Status::ok)return 0;
  if(x<INT_MIN||x>INT_MAX||y<INT_MIN||y>INT_MAX||width<0||width>INT_MAX||
     usage.paragraphs>=expected_paragraphs||usage.lines>=options.max_lines||
     usage.shape_calls>=options.paragraph.shape_calls||usage.text_result_bytes>=options.max_text_result_bytes) {
    status=Status::limit;return 0;
  }
  contextual_text::ParagraphOptions p;
  p.size_q1000=box.style.font_size*1000;
  p.width_q1000=static_cast<std::int32_t>(width);
  p.line_height_q1000=box.style.line_height>0?box.style.line_height*1000:0;
  p.nowrap=box.style.white_space_nowrap;p.emergency_wrap=box.style.overflow_wrap_anywhere;
  auto limits=options.paragraph;
  limits.lines=std::min(limits.lines,options.max_lines-usage.lines);
  limits.shape_calls-=usage.shape_calls;
  limits.result_bytes=std::min(limits.result_bytes,options.max_text_result_bytes-usage.text_result_bytes);
  contextual_text::LayoutParagraph entry;
  contextual_text::ParagraphUsage local;
  status=contextual_text::layout_paragraph(options.binding,box.node->text,p,limits,work,entry.paragraph,&local);
  usage.shape_calls+=local.shape_calls;usage.text_result_bytes+=local.result_bytes;
  if(status!=Status::ok)return 0;
  usage.lines+=entry.paragraph.lines().size();++usage.paragraphs;
  entry.owner=&box;entry.color=box.style.color;entry.align=box.style.text_align;
  entry.content_width_q1000=static_cast<std::int32_t>(width);
  const auto height=std::int64_t(entry.paragraph.height_q1000());
  box.rect={project_size_edge(x),project_size_edge(y),
      project_size_extent(x,width),project_size_extent(y,height)};
  box.content_geometry={x,y,width,height,false};
  draft->paragraphs.push_back(std::move(entry));
  return height;
}
bool ContextualLayoutFrame::finalize(LayoutBoxPtr root) {
  using contextual_text::Status;
  if(status!=Status::ok||!root||usage.paragraphs!=expected_paragraphs) {
    if(status==Status::ok)status=Status::invalid;
    return false;
  }
  for(auto& p:draft->paragraphs) {
    const auto x=p.owner->content_geometry.x,y=p.owner->content_geometry.y;
    if(x<INT_MIN||x>INT_MAX||y<INT_MIN||y>INT_MAX) {status=Status::limit;return false;}
    for(const auto& line:p.paragraph.lines()) {
      if(!work) {status=Status::limit;return false;}
      --work;
      // Overwide lines retain signed alignment; center/end may start before
      // the content edge. Every public paint origin must remain representable.
      const auto slack=std::int64_t(p.content_width_q1000)-line.metrics.advance_x;
      const auto dx=p.align==TextAlign::Center?slack/2:p.align==TextAlign::End?slack:0;
      const auto baseline=y+line.baseline_y_q1000;
      if(x+dx<INT_MIN||x+dx>INT_MAX||baseline<INT_MIN||baseline>INT_MAX) {
        status=Status::limit;return false;
      }
    }
    p.origin_x_q1000=static_cast<std::int32_t>(x);p.origin_y_q1000=static_cast<std::int32_t>(y);
  }
  std::vector<LayoutBox*> pending{root.get()};std::size_t count=0;
  while(!pending.empty()) {
    auto* box=pending.back();pending.pop_back();
    if(++count>usage.boxes||!work) {status=Status::limit;return false;}
    --work;
    box->text_layout_cache=TextLayoutCache{};
    box->text_layout_cache.available_width=-1;
    for(auto& child:box->children)pending.push_back(child.get());
  }
  if(count!=usage.boxes) {status=Status::invalid;return false;}
  draft->tree=std::move(root);return true;
}
}
