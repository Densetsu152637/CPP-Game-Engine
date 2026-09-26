//
// Created by Nicholas on 01/05/26.
//

#pragma once

#include "../core/engine.h"

void runEngine(Engine* e);

void glfw_main(Engine* engine, IDisplayManager* display, Logger* logger = nullptr);
