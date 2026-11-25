#include "doctest.h"
#include "uci.h"

#include <string_view>

TEST_CASE("engine identity is set") {
    CHECK(std::string_view(endgame::ENGINE_NAME) == "endgame");
}
