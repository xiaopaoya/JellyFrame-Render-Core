#include "contextual_text_layout_internal.h"
#include "computed_size_geometry.h"
#include "render_core/text_normalization.h"
#include <algorithm>
#include <climits>

namespace jellyframe::contextual_text {
const LayoutBox* LayoutResult::tree() const noexcept {return state_?state_->tree.get():nullptr;}
const std::vector<LayoutParagraph>& LayoutResult::paragraphs() const noexcept {
  static const std::vector<LayoutParagraph> empty;
  return state_?state_->paragraphs:empty;
}
const Node* LayoutResult::text_owner(std::size_t paragraph, std::size_t owner) const noexcept {
  if (!state_ || paragraph >= state_->paragraphs.size() || !state_->source_is_current())
    return nullptr;
  const auto& shared = state_->shared_owners;
  const auto entry = std::lower_bound(shared.begin(), shared.end(), paragraph,
      [](const SharedTextOwners& run, std::size_t index) { return run.paragraph_index < index; });
  if (entry != shared.end() && entry->paragraph_index == paragraph)
    return owner < entry->owners.size() ? entry->owners[owner].node : nullptr;
  return owner == 0 ? state_->paragraphs[paragraph].owner->node : nullptr;
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
bool plain(const Style& s, bool allow_height = false) {
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
  if((!allow_height&&s.height>=0)||(s.min_height>=0&&!(allow_height&&s.min_height==0))||
     s.max_height>=0||s.height_percent>=0||
     s.min_height_percent>=0||s.max_height_percent>=0)return false;
  return true;
}
bool uniform_inline_style(const Style& run, const Style& member) {
  return member.display == Display::Inline && zero(member.margin) && zero(member.padding) &&
      !member.background_color.a && member.font_size == run.font_size &&
      member.font_weight == run.font_weight && member.font_family_hash == run.font_family_hash &&
      member.line_height == run.line_height && member.text_align == run.text_align &&
      !member.white_space_nowrap && member.overflow_wrap_anywhere == run.overflow_wrap_anywhere &&
      member.color.r == run.color.r && member.color.g == run.color.g &&
      member.color.b == run.color.b && member.color.a == run.color.a;
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
  struct Pending {
    const RenderObject* object;
    std::size_t depth;
    bool flex_item;
    const Style* shared_style = nullptr;
  };
  std::vector<Pending> pending{{&root,1,false}};
  while(!pending.empty()) {
    const auto next=pending.back();pending.pop_back();
    if(usage.boxes==max_boxes||next.depth>max_depth||!work)return status=Status::limit;
    ++usage.boxes;--work;
    const auto& o=*next.object;
    if(o.children.size()>max_boxes-usage.boxes||pending.size()>max_boxes-usage.boxes-o.children.size())return status=Status::limit;
    if(!plain(o.style, (o.style.display == Display::Flex && o.style.flex_direction == FlexDirection::Column) ||
                     next.flex_item) ||
       !o.node||o.contents_sources)return status=Status::invalid;
    auto* shared_style = next.shared_style;
    if (shared_style && !uniform_inline_style(*shared_style, o.style)) return status = Status::invalid;
    if(o.type==RenderObjectType::Text) {
      const auto q=std::int64_t(o.style.font_size)*1000;
      if(o.node->type!=NodeType::Text||!o.children.empty()||o.style.font_family_hash!=options.font_family_hash||
         o.style.font_weight!=options.binding.weight()||q<=0||q>INT_MAX||
         !zero(o.style.margin)||!zero(o.style.padding)||o.style.background_color.a)return status=Status::invalid;
      if((!shared_style && ++expected_paragraphs>options.max_paragraphs) ||
         o.node->text.size()>options.max_source_bytes-usage.source_bytes)return status=Status::limit;
      usage.source_bytes+=o.node->text.size();
    } else if (shared_style) {
      if (o.type != RenderObjectType::Inline || o.node->type != NodeType::Element ||
          o.node->tag_name != "span" || o.style.width >= 0 || o.style.width_percent >= 0 ||
          o.style.min_width >= 0 || o.style.max_width >= 0 ||
          o.style.min_width_percent >= 0 || o.style.max_width_percent >= 0)
        return status = Status::invalid;
    } else {
      if((o.type!=RenderObjectType::View&&o.type!=RenderObjectType::Block)||
         o.node->type!=NodeType::Element||!ordinary(o.node->tag_name))return status=Status::invalid;
      if(o.style.display==Display::Flex) {
        const bool row = o.style.flex_direction == FlexDirection::Row;
        const bool wrapped_row = row && o.style.flex_wrap == 1;
        if((o.style.flex_wrap!=0&&!wrapped_row)||o.style.width<0||o.children.empty()||
           (!row&&(o.style.height<0||o.style.height_percent>=0)))return status=Status::invalid;
        if (wrapped_row && (o.style.width_percent >= 0 || o.style.min_width >= 0 ||
            o.style.max_width >= 0 || o.style.min_width_percent >= 0 || o.style.max_width_percent >= 0 ||
            o.style.flex_grow != 0 || o.style.align_content != JustifyContent::Start ||
            o.style.row_gap < 0 || o.style.column_gap < 0 ||
            o.style.box_sizing_border_box ||
            !zero(o.style.margin) || !zero(o.style.padding) || !zero(o.style.border_width)))
          return status=Status::invalid;
        if (!row && (o.style.width_percent >= 0 || o.style.min_width >= 0 || o.style.max_width >= 0))
          return status=Status::invalid;
        if (!row && (!zero(o.style.padding) || !zero(o.style.border_width) || o.style.box_sizing_border_box))
          return status=Status::invalid;
        if (!row && (o.style.min_height >= 0 || o.style.max_height >= 0))
          return status=Status::invalid;
        std::int64_t column_occupied = static_cast<std::int64_t>(o.style.row_gap) *
            static_cast<std::int64_t>(o.children.size() - 1);
        for(const auto& item:o.children) {
          if(!item||item->type!=RenderObjectType::Block||item->style.display!=Display::Block||
             item->node==nullptr||item->node->type!=NodeType::Element||item->children.size()!=1||
             item->children[0]==nullptr||item->children[0]->type!=RenderObjectType::Text)
            return status=Status::invalid;
          if (wrapped_row && (!plain(item->style) || item->style.width < 0 ||
              item->style.width_percent >= 0 || item->style.min_width > 0 || item->style.max_width >= 0 ||
              item->style.min_width_percent >= 0 || item->style.max_width_percent >= 0 ||
              item->style.flex_grow != 0 || item->style.flex_shrink != 0 ||
              (item->style.flex_basis >= 0 && (item->style.flex_basis_kind != FlexBasisKind::Length ||
                  item->style.flex_basis != item->style.width)) ||
              item->style.flex_order != 0 || item->style.box_sizing_border_box || !zero(item->style.margin) ||
              !zero(item->style.padding) || !zero(item->style.border_width)))
            return status=Status::invalid;
          if (!row) {
            if (!plain(item->style, true) || item->style.height < 0 || item->style.flex_grow != 0 ||
                item->style.flex_basis >= 0 || item->style.flex_basis_kind != FlexBasisKind::Length ||
                item->style.width < 0 || item->style.width_percent >= 0 ||
                item->style.min_width >= 0 || item->style.max_width >= 0 ||
                item->style.min_width_percent >= 0 || item->style.max_width_percent >= 0 ||
                (item->style.min_height >= 0 && item->style.min_height != 0) || item->style.max_height >= 0 ||
                !zero(item->style.margin) || !zero(item->style.padding) ||
                !zero(item->style.border_width) || item->style.box_sizing_border_box)
              return status=Status::invalid;
            column_occupied += item->style.height;
          }
        }
        if (!row && column_occupied > o.style.height) return status=Status::invalid;
      } else if(o.style.display!=Display::Block) return status=Status::invalid;
      std::size_t inline_children = 0;
      for (const auto& child : o.children) {
        if (child && (child->type == RenderObjectType::Text || child->type == RenderObjectType::Inline))
          ++inline_children;
      }
      const bool direct_text = o.children.size() == 1 && o.children.front() &&
          o.children.front()->type == RenderObjectType::Text;
      if (inline_children && !direct_text) {
        const auto font_size = std::int64_t(o.style.font_size) * 1000;
        if (!shared_inline || inline_children != o.children.size() || o.style.display != Display::Block ||
            o.style.white_space_nowrap || font_size <= 0 || font_size > INT_MAX ||
            o.style.font_family_hash != options.font_family_hash ||
            o.style.font_weight != options.binding.weight())
          return status = Status::invalid;
        if (++expected_paragraphs > options.max_paragraphs) return status = Status::limit;
        shared_style = &o.style;
      }
    }
    for(auto i=o.children.rbegin();i!=o.children.rend();++i) {
      if(!*i)return status=Status::invalid;
      pending.push_back({i->get(),next.depth+1,
          o.style.display==Display::Flex && o.style.flex_direction==FlexDirection::Column, shared_style});
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
  return prepare_paragraph(box, box.node->text, x, y, width, false);
}

std::int64_t ContextualLayoutFrame::prepare_shared(LayoutBox& host, std::int64_t x,
    std::int64_t y, std::int64_t width) {
  using contextual_text::Status;
  if (status != Status::ok) return 0;
  for (const auto& entry : draft->paragraphs) {
    if (entry.owner == &host) return prepare_paragraph(host, {}, x, y, width, true);
  }
  const auto index = draft->paragraphs.size();
  contextual_text::SharedTextOwners shared{index, {}};
  std::string source;
  const auto collect = [&](auto&& self, const LayoutBox& parent) -> bool {
    if (parent.node->children.size() > work) {
      work = 0;
      status = Status::limit;
      return false;
    }
    work -= parent.node->children.size();
    auto rendered = parent.children.begin();
    for (const auto& child : parent.node->children) {
      if (rendered != parent.children.end() && (*rendered)->node == child.get()) {
        const auto& box = **rendered++;
        if (child->type != NodeType::Text) {
          if (!self(self, box)) return false;
          continue;
        }
      } else {
        // RenderTree drops standalone collapsible whitespace.
        if (child->type != NodeType::Text) {
          status = Status::invalid;
          return false;
        }
        if (child->text.size() > work) {
          work = 0;
          status = Status::limit;
          return false;
        }
        work -= child->text.size();
        if (!is_collapsible_whitespace_text(*child)) {
          status = Status::invalid;
          return false;
        }
        if (child->text.size() > options.max_source_bytes - usage.source_bytes) {
          status = Status::limit;
          return false;
        }
        usage.source_bytes += child->text.size();
      }
      if (shared.owners.size() == 128 || child->text.size() > options.max_source_bytes - source.size() ||
          child->text.size() > work) {
        status = Status::limit;
        return false;
      }
      work -= child->text.size();
      if (!child->text.empty()) {
        shared.owners.push_back({child.get(), child->text.size()});
        source += child->text;
      }
    }
    if (rendered != parent.children.end()) {
      status = Status::invalid;
      return false;
    }
    return true;
  };
  if (!collect(collect, host)) return 0;

  const auto old_capacity = draft->shared_owners.capacity();
  draft->shared_owners.push_back(std::move(shared));
  const auto owner_bytes = draft->shared_owners.back().owners.capacity() * sizeof(contextual_text::InlineTextOwner);
  const auto table_bytes = (draft->shared_owners.capacity() - old_capacity) * sizeof(contextual_text::SharedTextOwners);
  if (owner_bytes + table_bytes > options.max_text_result_bytes - usage.text_result_bytes) {
    status = Status::limit;
    return 0;
  }
  usage.text_result_bytes += owner_bytes + table_bytes;
  return prepare_paragraph(host, source, x, y, width, true);
}

std::int64_t ContextualLayoutFrame::prepare_paragraph(LayoutBox& box, std::string_view source,
    std::int64_t x, std::int64_t y, std::int64_t width, bool shared) {
  using contextual_text::Status;
  if(status!=Status::ok)return 0;
  if(x<INT_MIN||x>INT_MAX||y<INT_MIN||y>INT_MAX||width<0||width>INT_MAX) {
    status=Status::limit;return 0;
  }
  const auto place = [&](std::int64_t height) {
    box.content_geometry = {x, y, width, height, false};
    if (!shared) {
      box.rect = {project_size_edge(x), project_size_edge(y),
          project_size_extent(x, width), project_size_extent(y, height)};
    }
  };
  for(auto& entry:draft->paragraphs)if(entry.owner==&box) {
    if(entry.content_width_q1000!=width) {status=Status::invalid;return 0;}
    const auto height=std::int64_t(entry.paragraph.height_q1000());
    place(height);
    entry.color=box.style.color;entry.align=box.style.text_align;
    return height;
  }
  if(usage.paragraphs>=expected_paragraphs||usage.lines>=options.max_lines||
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
  status=contextual_text::layout_paragraph(options.binding,source,p,limits,work,entry.paragraph,&local);
  usage.shape_calls+=local.shape_calls;usage.text_result_bytes+=local.result_bytes;
  if(status!=Status::ok)return 0;
  usage.lines+=entry.paragraph.lines().size();++usage.paragraphs;
  entry.owner=&box;entry.color=box.style.color;entry.align=box.style.text_align;
  entry.content_width_q1000=static_cast<std::int32_t>(width);
  const auto height=std::int64_t(entry.paragraph.height_q1000());
  place(height);
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
