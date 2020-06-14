//
//  ImguiLogger.hpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#pragma once

#include "imgui_cvlog.h"

#include <memory>
#include <vector>

namespace ImGui
{
namespace CVLog
{

// Image

struct Image
{
    std::vector<uint8_t> data;
    int bytesPerRow;
    int width;
    int height;
};
using ImagePtr = std::shared_ptr<Image>;

void UpdateImage(const char* windowName,
                 const ImagePtr& image);

// Plot

void AddPlotValue(const char* windowName,
                  const char* groupName,
                  double yValue,
                  double xValue,
                  const char* style = nullptr);


} // CVLog
} // ImGui
