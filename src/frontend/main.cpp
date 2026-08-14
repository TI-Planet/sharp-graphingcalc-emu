#include "app.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <memory>

namespace
{
    std::unique_ptr<FrontendApp> app;
}

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE int el9950_web_load_rom(const char* path) {
    return app && app->load_rom(path) ? 1 : 0;
}
void web_frame(void*) {
    if (app)
        app->frame();
}
#endif

int main(int argc, char** argv) {
    app = std::make_unique<FrontendApp>();
    if (!app->initialize())
        return 1;
    if (argc > 1)
        app->load_rom(argv[1]);
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(web_frame, nullptr, 0, true);
#else
    while (app->running())
        app->frame();
#endif
    app.reset();
    return 0;
}
