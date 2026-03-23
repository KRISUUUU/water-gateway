#include <cassert>

#include "dedup_service/dedup_service.hpp"

int main() {
    dedup_service::DedupService dedup(1000);

    assert(!dedup.seen_recently("ABC", 1000));
    dedup.remember("ABC", 1000);
    assert(dedup.seen_recently("ABC", 1500));
    assert(!dedup.seen_recently("ABC", 2501));

    return 0;
}
