#include "inline_fragment_lines_reader.h"
namespace jellyframe::detail {
InlineLinePlanStatus plan_inline_fragment_lines(const InlineMeasuredPiece* input,std::size_t count,const InlineLinePlanOptions& options,InlineLinePlan& output,InlineLinePlanLimits limits,std::size_t* work_used,std::size_t* capacity_used) {
    if(work_used)*work_used=0;
    if(capacity_used)*capacity_used=0;
    if(count && !input)return InlineLinePlanStatus::InvalidInput;
    struct Dense {const InlineMeasuredPiece* data;bool read(std::size_t i,InlineMeasuredPiece& out)const{out=data[i];return true;}};
    return plan_inline_fragment_lines_read(Dense{input},count,options,output,limits,work_used,capacity_used);
}
}
