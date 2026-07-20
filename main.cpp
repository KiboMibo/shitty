#include "application.h"
#include "composer.h"
#include "font_resolver.h"

#include <std/mem/obj_pool.h>

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    int status = 1;
    try {
        stl::ObjPool::Ref pool = stl::ObjPool::fromMemory();
        Composer composer;
        composer.pool = pool.mutPtr();
        composer.application = Application::create(composer);
        status = composer.application->run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << std::endl;
    }
    finalizeFontconfig();
    return status;
}
