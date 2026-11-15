#pragma once
#include "ap/engine.hpp"
namespace ap {
struct Choice {
    Str category, id, label, value;
    bool checked = true, required = false;
};
List<Choice> choices(const Installer& installer);
void choose(Installer& installer, const Choice& choice, bool checked);
Str prepareIcon(Installer& installer);
Status tui(Installer& installer);
Status gui(Installer& installer, bool smoke = false);
} // namespace ap
