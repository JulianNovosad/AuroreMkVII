/**
 * @file interlock_controller_test.cpp
 * @brief Unit tests for InterlockController (hardware-free paths)
 *
 * Tests exercise software-only methods that do not require GPIO hardware.
 * init() is intentionally NOT called so gpio_map remains null, exercising
 * the null guards in update_inhibit_output() and update_status_led().
 */

#include "aurore/interlock_controller.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace aurore;

static int tests_run = 0;
static int tests_passed = 0;

#define CHECK(cond, msg)                                          \
    do {                                                          \
        ++tests_run;                                              \
        if (!(cond)) {                                            \
            std::fprintf(stderr, "FAIL [%s]: %s\n", __func__, msg); \
            return false;                                         \
        } else {                                                  \
            ++tests_passed;                                       \
        }                                                         \
    } while (0)

// ---------------------------------------------------------------------------
// test_initial_state_is_unknown
// ---------------------------------------------------------------------------
static bool test_initial_state_is_unknown() {
    InterlockController ic;
    CHECK(ic.get_state() == InterlockState::UNKNOWN,
          "freshly constructed controller state should be UNKNOWN");
    return true;
}

// ---------------------------------------------------------------------------
// test_force_state_changes_state
// ---------------------------------------------------------------------------
static bool test_force_state_changes_state() {
    InterlockController ic;

    ic.force_state(InterlockState::CLOSED);
    CHECK(ic.get_state() == InterlockState::CLOSED,
          "state should be CLOSED after force_state(CLOSED)");

    ic.force_state(InterlockState::OPEN);
    CHECK(ic.get_state() == InterlockState::OPEN,
          "state should be OPEN after force_state(OPEN)");

    return true;
}

// ---------------------------------------------------------------------------
// test_actuation_allowed_only_when_closed
// ---------------------------------------------------------------------------
static bool test_actuation_allowed_only_when_closed() {
    InterlockController ic;

    ic.force_state(InterlockState::OPEN);
    CHECK(!ic.is_actuation_allowed(),
          "actuation should NOT be allowed when state is OPEN");

    ic.force_state(InterlockState::CLOSED);
    CHECK(ic.is_actuation_allowed(),
          "actuation SHOULD be allowed when state is CLOSED");

    ic.force_state(InterlockState::FAULT);
    CHECK(!ic.is_actuation_allowed(),
          "actuation should NOT be allowed when state is FAULT");

    return true;
}

// ---------------------------------------------------------------------------
// test_get_status_reflects_state
// ---------------------------------------------------------------------------
static bool test_get_status_reflects_state() {
    InterlockController ic;

    ic.force_state(InterlockState::CLOSED);
    {
        InterlockStatus s = ic.get_status();
        CHECK(s.state == InterlockState::CLOSED,
              "status.state should be CLOSED");
        CHECK(!s.actuation_inhibited,
              "actuation_inhibited should be false when CLOSED");
    }

    ic.force_state(InterlockState::FAULT);
    {
        InterlockStatus s = ic.get_status();
        CHECK(s.state == InterlockState::FAULT,
              "status.state should be FAULT");
        CHECK(s.actuation_inhibited,
              "actuation_inhibited should be true when FAULT");
    }

    return true;
}

// ---------------------------------------------------------------------------
// test_watchdog_feed_increments_counter
// ---------------------------------------------------------------------------
static bool test_watchdog_feed_increments_counter() {
    InterlockConfig cfg;
    cfg.enable_watchdog = true;
    InterlockController ic(cfg);

    const uint64_t before = ic.get_status().watchdog_feeds;
    ic.watchdog_feed();
    const uint64_t after = ic.get_status().watchdog_feeds;

    CHECK(after == before + 1,
          "watchdog_feeds should increment by 1 after watchdog_feed()");

    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    struct { const char* name; bool (*fn)(); } tests[] = {
        { "test_initial_state_is_unknown",       test_initial_state_is_unknown       },
        { "test_force_state_changes_state",      test_force_state_changes_state      },
        { "test_actuation_allowed_only_when_closed", test_actuation_allowed_only_when_closed },
        { "test_get_status_reflects_state",      test_get_status_reflects_state      },
        { "test_watchdog_feed_increments_counter", test_watchdog_feed_increments_counter },
    };

    for (auto& t : tests) {
        std::printf("[ RUN  ] %s\n", t.name);
        bool ok = t.fn();
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
    }

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
