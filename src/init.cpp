#include "init.h"

#include "attacks.h"
#include "eval_hce.h"
#include "position.h"

namespace endgame {

void init_all() {
    attacks::init();
    zobrist::init();
    hce::init_default_params();
}

}  // namespace endgame
