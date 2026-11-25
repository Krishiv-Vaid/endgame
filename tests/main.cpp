#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "init.h"

int main(int argc, char** argv) {
    endgame::init_all();
    doctest::Context ctx(argc, argv);
    return ctx.run();
}
