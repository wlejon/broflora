#include "broflora/api/api.h"
#include "embed/embed.h"

extern "C" void bronze_flora_main();

namespace broflora::api {

void registerFloraNativeHelpers();

void installFlora() {
    registerFloraNativeHelpers();
    bronze::embed::runEntry(bronze_flora_main);
}

} // namespace broflora::api
