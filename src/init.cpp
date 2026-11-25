#include "init.h"

#include "attacks.h"
#include "position.h"

namespace endgame {

void init_all() {
    attacks::init();
    zobrist::init();
}

}  // namespace endgame
