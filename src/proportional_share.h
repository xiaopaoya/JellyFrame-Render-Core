#pragma once
#include <cstdint>
#include <limits>

namespace jellyframe::detail {
// Exact floor(amount*weight/total), with weight<=total. The common UI case
// needs one multiplication; extreme coordinates use bounded integer steps.
inline std::uint64_t proportional_share(std::uint64_t amount,std::uint64_t weight,std::uint64_t total) {
    if(amount==0 || weight==0 || total==0)return 0;
    if(amount<=std::numeric_limits<std::uint64_t>::max()/weight)return amount*weight/total;
    std::uint64_t quotient=0,remainder=0;
    for(int bit=63;bit>=0;--bit) {
        quotient*=2;
        if(remainder>=total-remainder){remainder-=total-remainder;++quotient;}
        else remainder*=2;
        if((amount>>bit)&1U) {
            if(remainder>=total-weight){remainder-=total-weight;++quotient;}
            else remainder+=weight;
        }
    }
    return quotient;
}
}
