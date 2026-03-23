#define HOST_TEST_BUILD
#include "host_test_stubs.hpp"
#include "dedup_service/dedup_service.hpp"
#include <cassert>
#include <cstdio>

using namespace dedup_service;

static void test_new_key_not_seen() {
    auto& svc = DedupService::instance();
    svc.clear();
    svc.set_window_ms(5000);

    assert(!svc.seen_recently("AABBCCDD", 1000));
    printf("  PASS: new key not seen\n");
}

static void test_remembered_key_is_seen() {
    auto& svc = DedupService::instance();
    svc.clear();
    svc.set_window_ms(5000);

    svc.remember("AABBCCDD", 1000);
    assert(svc.seen_recently("AABBCCDD", 1500));
    printf("  PASS: remembered key is seen\n");
}

static void test_expired_key_not_seen() {
    auto& svc = DedupService::instance();
    svc.clear();
    svc.set_window_ms(5000);

    svc.remember("AABBCCDD", 1000);
    // 7 seconds later, past the 5s window
    assert(!svc.seen_recently("AABBCCDD", 8000));
    printf("  PASS: expired key not seen\n");
}

static void test_different_keys_independent() {
    auto& svc = DedupService::instance();
    svc.clear();
    svc.set_window_ms(5000);

    svc.remember("AABB", 1000);
    assert(svc.seen_recently("AABB", 2000));
    assert(!svc.seen_recently("CCDD", 2000));
    printf("  PASS: different keys are independent\n");
}

static void test_prune_removes_old() {
    auto& svc = DedupService::instance();
    svc.clear();
    svc.set_window_ms(1000);

    svc.remember("A", 100);
    svc.remember("B", 200);
    svc.remember("C", 2000);
    assert(svc.entry_count() == 3);

    svc.prune(1500); // A and B should be pruned (100+1000=1100, 200+1000=1200)
    assert(svc.entry_count() == 1);
    printf("  PASS: prune removes old entries\n");
}

static void test_entry_count() {
    auto& svc = DedupService::instance();
    svc.clear();

    assert(svc.entry_count() == 0);
    svc.remember("A", 100);
    svc.remember("B", 200);
    assert(svc.entry_count() == 2);
    svc.clear();
    assert(svc.entry_count() == 0);
    printf("  PASS: entry count tracking\n");
}

int main() {
    printf("=== test_dedup ===\n");
    test_new_key_not_seen();
    test_remembered_key_is_seen();
    test_expired_key_not_seen();
    test_different_keys_independent();
    test_prune_removes_old();
    test_entry_count();
    printf("All dedup tests passed.\n");
    return 0;
}
