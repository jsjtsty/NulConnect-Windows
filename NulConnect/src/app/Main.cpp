#include "pch.h"
#include "app/App.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    return nc::App::Run(instance, commandLine, showCommand);
}
