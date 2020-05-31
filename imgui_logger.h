//
//  ImguiLogger.hpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ImGui
{
namespace Logger
{

struct Image
{
    std::vector<uint8_t> data;
    int bytesPerRow;
    int width;
    int height;
};
using ImagePtr = std::shared_ptr<Image>;

// Run arbitrary ImGui code for each frame (e.g add UI element).
void SetPerFrameCallback(const char* callbackName,
                         const std::function<void(void)>& callback);

void SetWindowRenderExtraCallback(const char* windowName,
                                  const char* callbackName,
                                  const std::function<void(void)>& callback);

void UpdateImage(const char* windowName,
                 const ImagePtr& image);

void AddPlotValue(const char* windowName,
                  const char* groupName,
                  double yValue,
                  double xValue);

void Render();

} // Logger
} // ImGui
