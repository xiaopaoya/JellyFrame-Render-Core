#include "render_core/style.h"
#include "render_core/css_parser.h"
#include "render_core/render_tree.h"
#include "render_core/style_repaint.h"
#include "../../src/computed_size_values.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

static bool fail_allocation = false;
static std::size_t allocations = 0;
static std::size_t last_allocation_bytes = 0;
void* operator new(std::size_t size) {
    if (fail_allocation) throw std::bad_alloc();
    if (auto* data = std::malloc(size ? size : 1)) { ++allocations; last_allocation_bytes=size; return data; }
    throw std::bad_alloc();
}
void operator delete(void* data) noexcept { std::free(data); }
void operator delete(void* data, std::size_t) noexcept { std::free(data); }

using namespace jellyframe;
using namespace jellyframe::detail;
void check(bool success, const char* message) { if (!success) throw std::runtime_error(message); }
std::int64_t units(const ComputedSizeStorage& storage, SizeProperty property, SizeKind expected) {
    SizeKind kind;
    std::int64_t value = -1;
    check(storage.get(property,kind,value) && kind == expected, "computed slot exists with correct kind");
    return value;
}
void signed_inset_storage_contract() {
    ComputedSizeStorage storage;
    const auto before=allocations;
    check(storage.set(SizeProperty::InsetLeft,SizeKind::Pixels,-7),"signed integral inset accepted");
    check(storage.empty() && allocations==before,"integral inset stays allocation free");
    check(storage.set(SizeProperty::InsetLeft,SizeKind::Percent,-1000),"negative coefficient bound accepted");
    const auto payload_bytes=last_allocation_bytes;
    check(units(storage,SizeProperty::InsetLeft,SizeKind::Percent)==-1000000,"signed percentage coefficient retained");
    check(storage.set_length_percentage(SizeProperty::InsetTop,-2147483647.0,1000),"signed mixed bounds accepted");
    auto saved=storage;
    for(double invalid:{-1000.001,1000.001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        check(!storage.set(SizeProperty::InsetTop,SizeKind::Percent,invalid),"out of range coefficient refuses");
        check(!storage.set_length_percentage(SizeProperty::InsetTop,1,invalid),"mixed coefficient refuses before narrowing");
        check(storage.equals(saved),"invalid coefficient leaves every source unit unchanged");
    }
    for(unsigned operation=0;operation<3;++operation) {
        fail_allocation=true;bool failed=false;
        try {
            if(operation==0) storage.set(SizeProperty::InsetLeft,SizeKind::Pixels,-0.25);
            if(operation==1) storage.set_length_percentage(SizeProperty::InsetTop,1.25,-37.5);
            if(operation==2) storage.clear(SizeProperty::InsetTop);
        } catch(const std::bad_alloc&) {failed=true;}
        fail_allocation=false;
        check(failed && storage.equals(saved),"allocation failure preserves mixed and signed snapshot");
    }
    std::int64_t length=0,percentage=0;SizeKind kind;
    check(storage.get(SizeProperty::InsetTop,kind,length,percentage) &&
        length==-2147483647000LL && percentage==1000000,"mixed signed extremes survive compact coefficient storage");
    kind=SizeKind::Percent; length=123;
    check(!storage.get(SizeProperty::InsetTop,kind,length) && kind==SizeKind::Percent && length==123,
        "legacy getter refuses mixed percentage without changing outputs");
    const auto refused_allocations=allocations;
    for (unsigned index=0;index<7;++index) {
        ComputedSizeStorage dimension;
        check(dimension.set_length_percentage(static_cast<SizeProperty>(index),1.25,25),
            "dimension slots accept bounded mixed units");
        SizeKind typed_kind; std::int64_t length=0, percent=0;
        check(dimension.read_typed(static_cast<SizeProperty>(index),typed_kind,length,percent) &&
            typed_kind == SizeKind::LengthPercentage && length == 1250 && percent == 25000,
            "typed mixed presence survives for dimensions");
        check(!dimension.get(static_cast<SizeProperty>(index),typed_kind,length) && length == 1250,
            "legacy getter continues to hide mixed dimensions");
    }
    check(!storage.set_length_percentage(SizeProperty::InsetTop,2147483648.0,0) && storage.equals(saved),"mixed length overflow refuses atomically");
    const auto copies=allocations;
    auto copy=storage;
    check(allocations==copies && copy.equals(storage),"eleven-slot copy shares payload without allocation");
    check(copy.set_length_percentage(SizeProperty::InsetTop,1.25,-37.5) && allocations==copies+1 &&
        storage.equals(saved),"inset mutation allocates one isolated replacement");
    std::cout << "computed payload allocation=" << payload_bytes << " bytes; owner=" << sizeof(ComputedSizeStorage) << '\n';
}

int main() {
    try {
        Style style;
        const auto initial = allocations;
        for (unsigned i=0; i<10000; ++i) check(apply_computed_size(style,SizeProperty::Width,SizeKind::Pixels,12),"integer accepted");
        check(style.computed_sizes.empty() && allocations == initial, "integer styles allocate no precision storage");
        fail_allocation = true;
        bool first_failed = false;
        try { apply_computed_size(style,SizeProperty::Height,SizeKind::Pixels,1.25); }
        catch (const std::bad_alloc&) { first_failed = true; }
        fail_allocation = false;
        check(first_failed && style.height == -1 && style.computed_sizes.empty(),
              "first allocation failure preserves empty state and scalar");
        check(apply_computed_size(style,SizeProperty::Width,SizeKind::Pixels,12.25), "fraction accepted");
        check(allocations == initial+1, "first fractional dimension allocates one owner");
        check(units(style.computed_sizes,SizeProperty::Width,SizeKind::Pixels)==12250,"exact Q1000 value");
        const auto shared_sizes = style.computed_sizes;
        check(allocations == initial+1,"precision owner copy allocates nothing");
        Style copy = style;
        // Checked MSVC strings/vectors allocate iterator proxies on Style copy.
        // The precision subobject still shares the original immutable payload.
        check(copy.computed_sizes.equals(shared_sizes),"Style copy shares immutable precision");
        check(apply_computed_size(copy,SizeProperty::Height,SizeKind::Percent,37.5),"fractional percent accepted");
        check(units(copy.computed_sizes,SizeProperty::Height,SizeKind::Percent)==37500,"percent remains precise");
        SizeKind kind; std::int64_t value;
        check(!style.computed_sizes.get(SizeProperty::Height,kind,value),"copy mutation leaves original unchanged");
        fail_allocation = true;
        bool failed = false;
        try { apply_computed_size(copy,SizeProperty::Width,SizeKind::Pixels,15.125); }
        catch (const std::bad_alloc&) { failed = true; }
        fail_allocation = false;
        check(failed && copy.width==12 && units(copy.computed_sizes,SizeProperty::Width,SizeKind::Pixels)==12250,
              "allocation failure preserves scalar and retained values");
        fail_allocation = true;
        failed = false;
        try { apply_computed_size(copy,SizeProperty::Width,SizeKind::Pixels,20); }
        catch (const std::bad_alloc&) { failed = true; }
        fail_allocation = false;
        check(failed && copy.width==12 && units(copy.computed_sizes,SizeProperty::Height,SizeKind::Percent)==37500,
              "partial clear allocation failure is atomic");
        for (double invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(),2147483648.0}) {
            check(!apply_computed_size(copy,SizeProperty::Width,SizeKind::Pixels,invalid),"invalid sizes refuse");
            check(copy.width==12 && units(copy.computed_sizes,SizeProperty::Width,SizeKind::Pixels)==12250,"refusal preserves value");
        }
        check(!copy.computed_sizes.set(static_cast<SizeProperty>(99),SizeKind::Pixels,1.5),"invalid index refuses");
        check(!copy.computed_sizes.set(SizeProperty::FlexBasis,SizeKind::Percent,1.5),"unsupported flex percent slot refuses");
        check(apply_computed_size(style,SizeProperty::Width,SizeKind::Pixels,12),"integral override clears residue");
        check(style.computed_sizes.empty(),"last slot clear releases owner");
        auto moved = std::move(copy.computed_sizes);
        check(copy.computed_sizes.empty() && units(moved,SizeProperty::Height,SizeKind::Percent)==37500,"move transfers owner");
        ComputedSizeStorage independent;
        check(independent.set(SizeProperty::Width,SizeKind::Pixels,12.25) &&
              independent.set(SizeProperty::Height,SizeKind::Percent,37.5) && independent.equals(moved),
              "logical equality does not depend on owner identity");
        auto node = make_element("div");
        node->set_attribute("style","width:12.25px;height:37.5%;min-width:4.125px;max-height:91.75px");
        StyleResolver resolver(CssParser{}.parse(""));
        auto computed = resolver.resolve(*node);
        check(units(computed.computed_sizes,SizeProperty::Width,SizeKind::Pixels)==12250,"CSS literal size retained");
        check(units(computed.computed_sizes,SizeProperty::Height,SizeKind::Percent)==37500,"CSS percentage retained");
        check(units(computed.computed_sizes,SizeProperty::MinWidth,SizeKind::Pixels)==4125,"min size retained");
        check(units(computed.computed_sizes,SizeProperty::MaxHeight,SizeKind::Pixels)==91750,"max size retained");
        node->set_attribute("style","width:12.25px;width:bad");
        check(units(resolver.resolve(*node).computed_sizes,SizeProperty::Width,SizeKind::Pixels)==12250,"invalid override preserves precision");
        node->set_attribute("style","width:12.25px;width:auto;height:3.5px;height:7px");
        check(resolver.resolve(*node).computed_sizes.empty(),"auto and integer overrides clear precision");
        node->set_attribute("style","width:12.25px;width:2em");
        check(resolver.resolve(*node).computed_sizes.empty(),"unmigrated relative-unit path clears stale residue");
        node->set_attribute("style","min-width:4.125px;min-width:unset;max-height:91.75px;max-height:none");
        check(resolver.resolve(*node).computed_sizes.empty(),"min/max reset keywords clear stale residues");
        node->set_attribute("style","width:12.1px;height:7px");
        auto before = RenderTreeBuilder(resolver).build(*node);
        auto layout = LayoutEngine(resolver).layout(*before,320,240);
        node->set_attribute("style","width:12.2px;height:7px");
        auto after = RenderTreeBuilder(resolver).build(*node);
        check(before->style.width == after->style.width &&
            !style_dirty_can_reuse_layout(*node,*before,*after,*layout,{}),
            "different retained dimensions invalidate reuse even with identical scalar proxies");
        for (unsigned index=0;index<7;++index) {
            ComputedSizeStorage storage;
            check(storage.set(static_cast<SizeProperty>(index),SizeKind::Pixels,0.0625),"all size slots accept fractional pixels");
            check(units(storage,static_cast<SizeProperty>(index),SizeKind::Pixels)==63,"shared encoder uses admitted decimal-half rounding");
        }
        signed_inset_storage_contract();
        std::cout << "Style bytes=" << sizeof(Style) << "; computed size storage tests passed\n";
    } catch (const std::exception& error) { fail_allocation=false;std::cerr<<error.what()<<'\n';return 1; }
}
