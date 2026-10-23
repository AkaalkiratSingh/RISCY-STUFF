#include "memory_safety/stack_guard.h"
#include "memory_safety/visualizer.h"
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond) do {                                                   \
    if (!(cond)) {                                                         \
        std::cerr << "  FAIL line " << __LINE__ << ": " #cond << "\n";     \
        ++failures;                                                        \
    }                                                                      \
} while (0)

#define CHECK_EQ(actual, expected) do {                                    \
    const auto got_  = (actual);                                           \
    const auto want_ = (expected);                                         \
    if (!(got_ == want_)) {                                                \
        std::cerr << "  FAIL line " << __LINE__ << ": " #actual            \
                  << " == " #expected << "\n";                             \
        ++failures;                                                        \
    }                                                                      \
} while (0)

using namespace risc201;

} // namespace

int main() {
    std::cout << "StackGuard: valid stack range\n";
    {
        StackGuard guard(16, 0);

        CHECK(!guard.checkBounds(16).has_value());
        CHECK(!guard.checkBounds(8).has_value());
        CHECK(!guard.checkBounds(0).has_value());

        CHECK(guard.isValidAddress(0));
        CHECK(guard.isValidAddress(15));
        CHECK(!guard.isValidAddress(16));
    }

    std::cout << "StackGuard: PUSH boundary protection\n";
    {
        StackGuard guard(16, 0);

        CHECK(!guard.checkPush(16).has_value());
        CHECK(!guard.checkPush(1).has_value());

        CHECK_EQ(guard.checkPush(0).value_or(ExceptionCode::NONE),
                 ExceptionCode::STACK_OVERFLOW);
    }

    std::cout << "StackGuard: POP boundary protection\n";
    {
        StackGuard guard(16, 0);

        CHECK(!guard.checkPop(0).has_value());
        CHECK(!guard.checkPop(15).has_value());

        CHECK_EQ(guard.checkPop(16).value_or(ExceptionCode::NONE),
                 ExceptionCode::STACK_UNDERFLOW);
    }

    std::cout << "StackGuard: invalid stack configuration\n";
    {
        StackGuard guard(0, 16);

        CHECK_EQ(guard.checkBounds(8).value_or(ExceptionCode::NONE),
                 ExceptionCode::INVALID_MEMORY_ACCESS);
    }
    
    std::cout << "StackVisualizer: renders stack contents and SP\n";
    {
        StackVisualizer visualizer;

        const std::vector<int32_t> memory = {
            10, 20, 30, 40, 50
        };

        const std::string output = visualizer.render(memory, 2, 5);

        CHECK(output.find("Stack (top -> bottom)") != std::string::npos);
        CHECK(output.find("SP = 2") != std::string::npos);
        CHECK(output.find("[2] 30  <-- SP") != std::string::npos);
        CHECK(output.find("[3] 40") != std::string::npos);
        CHECK(output.find("[4] 50") != std::string::npos);
    }
    
    if (failures == 0) {
        std::cout << "\nmemory_safety_test: all checks passed\n";
        return 0;
    }

    std::cerr << "\nmemory_safety_test: "
              << failures << " check(s) failed\n";
    return 1;
}
