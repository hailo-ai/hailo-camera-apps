#pragma once
#include <queue>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <tl/expected.hpp>
#include <signal.h>
#include <cxxopts/cxxopts.hpp>
#include <signal.h>
#include <condition_variable>
#include <mutex>
#include "scenarios/generate_pipeline.hpp"


void stop_encoders(std::shared_ptr<AppResources> app_resources)
{
    for (auto &enc_pair : app_resources->encoders)
    {
        enc_pair.second->stop();
    }
}

void start_encoders(std::shared_ptr<AppResources> app_resources)
{
    for (auto &enc_pair : app_resources->encoders)
    {
        enc_pair.second->start();
    }
}

void cause_segfault()
{
    int *p = nullptr;
    *p = 42; // This will cause a segmentation fault
}

