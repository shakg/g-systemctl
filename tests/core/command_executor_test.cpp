#include <gtest/gtest.h>
#include <g-systemctl/core/command_executor.hpp>

namespace gsystemctl {

TEST(CommandResultTest, ConstructionWithValues) {
    CommandResult result;
    result.exit_code = 1;
    result.stdout_output = "output";
    result.stderr_output = "error";
    
    EXPECT_EQ(result.exit_code, 1);
    EXPECT_EQ(result.stdout_output, "output");
    EXPECT_EQ(result.stderr_output, "error");
}

} // namespace gsystemctl