#pragma once

struct Composer;

struct Application {
    virtual int run(int argc, char* argv[]) = 0;

    static Application* create(Composer& composer);
};
