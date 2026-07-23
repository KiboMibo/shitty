#include "composer.h"

#include "cell_extra_store.h"

namespace stl {}
using namespace stl;

Composer::~Composer() noexcept {
    delete cellExtras;
}
