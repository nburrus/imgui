//
//  ImguiLogger.cpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#include "imgui_cvlog_demo.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"

#include <OpenGL/gl3.h>
#define GL_SILENCE_DEPRECATION 1

#include <thread>
#include <unordered_map>

namespace ImGui
{
namespace CVLog
{

#pragma mark - Images

class ImageWindow : public Window
{
public:
    void UpdateImage (const ImagePtr& newImage)
    {
        // Don't update it if it's not visible to save on CPU time.
        if (!isVisible())
            return;
        
        std::lock_guard<std::mutex> _ (concurrent.imageLock);
        concurrent.image = newImage;
    }
    
    bool Begin(bool* closed) override
    {
        // Uncomment along with the PopStyleVar to remove the extra padding.
        // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
        
        // Force no scrollbar.
        bool active = ImGui::Begin(name(), closed, ImGuiWindowFlags_NoScrollbar);
        
        // ImGui::PopStyleVar();
        return active;
    }
    
    void Render() override
    {
        ImagePtr imageToShow;
        
        {
            std::lock_guard<std::mutex> _ (concurrent.imageLock);
            imageToShow = concurrent.image;
        }
        
        if (!imageToShow)
            return;
        
        if (_textureID == 0)
        {
            // Create a OpenGL texture identifier
            glGenTextures(1, &_textureID);
            glBindTexture(GL_TEXTURE_2D, _textureID);

            // Setup filtering parameters for display
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        
        if (imageToShow->data.data() != _imageDataUploadedToTexture)
        {
            // Upload pixels into texture
            glBindTexture(GL_TEXTURE_2D, _textureID);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imageToShow->width, imageToShow->height, 0, GL_RED, GL_UNSIGNED_BYTE, imageToShow->data.data());
            _imageDataUploadedToTexture = imageToShow->data.data();
        }
        
        if (ImGui::Begin(name()))
        {
            //            ImGui::BulletText("ImageWindow content");
            //            ImGui::BulletText("Width: %d", imageToShow->width);
            //            ImGui::BulletText("Height: %d", imageToShow->height);

            float inputImageAspectRatio = float(imageToShow->height) / imageToShow->width;
            ImVec2 wSize = ImGui::GetContentRegionAvail();
            float windowContentAspectRatio = wSize.y / wSize.x;
            if (inputImageAspectRatio <  windowContentAspectRatio)
            {
                ImGui::Image((void*)(intptr_t)_textureID, ImVec2(wSize.x, wSize.x*inputImageAspectRatio));
            }
            else
            {
                ImGui::Image((void*)(intptr_t)_textureID, ImVec2(wSize.y/inputImageAspectRatio, wSize.y));
            }
        }
        ImGui::End();
    }
    
private:
    struct {
        std::mutex imageLock;
        ImagePtr image;
    } concurrent;
    
    GLuint _textureID = 0;
    uint8_t* _imageDataUploadedToTexture = nullptr;
};

void UpdateImage(const char* windowName,
                 const ImagePtr& image)
{
    ImageWindow* imWindow = FindWindow<ImageWindow> (windowName);
    
    // The window exists, just update the data.
    if (imWindow)
    {
        imWindow->UpdateImage (image);
        return;
    }
    
    // Need to create it, enqueue that in the list of tasks for the next frame;
    std::string windowNameCopy = windowName;
    RunOnceInImGuiThread([windowNameCopy,image](){
        ImageWindow* imWindow = FindOrCreateWindow<ImageWindow>(windowNameCopy.c_str());
        imWindow->UpdateImage(image);
    });
}

} // CVLog
} // ImGui

#pragma mark - Plot

namespace ImGui
{
namespace CVLog
{

class PlotWindow : public Window
{
public:
    void AddPlotValue(const char* groupName,
                      float yValue,
                      float xValue,
                      const char* style)
    {
        // Don't update it if it's not visible to save on CPU time.
        if (!isVisible())
            return;
        
        ImGuiID groupId = ImHashStr(groupName);
        std::lock_guard<std::mutex> _ (concurrent.lock);
        concurrent.dataSinceLastFrame.push_back({groupId,xValue,yValue});
        if (!concurrent.existingGroups.GetBool(groupId))
        {
            GroupToAdd group;
            group.name = groupName;
            group.style = style ? style : "";
            concurrent.addedGroupsSinceLastFrame.push_back(group);
        }
    }
            
    void Render() override
    {
        {
            std::lock_guard<std::mutex> _ (concurrent.lock);
            _cacheOfDataToAppend.swap (concurrent.dataSinceLastFrame);
            for (const auto& group : concurrent.addedGroupsSinceLastFrame)
            {
                ImGuiID groupId = ImHashStr(group.name.c_str());
                _groupData[groupId].name = group.name;
                if (!group.style.empty())
                {
                    parseAndFillStyle (group.style, _groupData[groupId]);
                }
                concurrent.existingGroups.SetBool(groupId, true);
            }
        }
        
        for (const auto& it : _cacheOfDataToAppend)
        {
            auto& groupData = _groupData[it.group];
            groupData.xData.push_back(it.xValue);
            groupData.yData.push_back(it.yValue);
            
            if (groupData.xData.size() == 1)
            {
                groupData.xMin = groupData.xMax = it.xValue;
                groupData.yMin = groupData.yMax = it.yValue;
            }
            else
            {
                groupData.xMin = std::min(groupData.xMin, it.xValue);
                groupData.xMax = std::max(groupData.xMax, it.xValue);
                groupData.yMin = std::min(groupData.yMin, it.yValue);
                groupData.yMax = std::max(groupData.yMax, it.yValue);
            }
            
            _dataBounds.xMin = std::min(_dataBounds.xMin, groupData.xMin);
            _dataBounds.xMax = std::max(_dataBounds.xMax, groupData.xMax);
            _dataBounds.yMin = std::min(_dataBounds.yMin, groupData.yMin);
            _dataBounds.yMax = std::max(_dataBounds.yMax, groupData.yMax);
        }
        _cacheOfDataToAppend.clear();
        
        if (_groupData.empty())
            return;
                
        if (Begin(nullptr))
        {
            if (_autoFitEnabled)
            {
                ImPlot::SetNextPlotLimitsX(_dataBounds.xMin, _dataBounds.xMax, ImGuiCond_Always);
                ImPlot::SetNextPlotLimitsY(_dataBounds.yMin, _dataBounds.yMax, ImGuiCond_Always);
            }
            
            ImVec2 plotSize = ImGui::GetContentRegionAvail();
            if (ImPlot::BeginPlot("##NoTitle" /* title */, nullptr /* xLabel */, nullptr /* yLabel */, plotSize))
            {
                if (ImPlot::IsXAxisAutoFitRequested() && ImPlot::IsYAxisAutoFitRequested())
                {
                    _autoFitEnabled = !_autoFitEnabled;
                }

                for (const auto& it : _groupData)
                {
                    if (it.second.hasCustomLineColor)
                        ImPlot::PushStyleColor(ImPlotCol_Line, it.second.lineColor);
                    
                    ImPlot::PlotLine(it.second.name.c_str(), it.second.xData.data(), it.second.yData.data(), (int)it.second.xData.size());
                    
                    if (it.second.hasCustomLineColor)
                        ImPlot::PopStyleColor();
                }
                
                ImPlot::EndPlot();
            }
        }
        ImGui::End();
    }
    
private:
    struct GroupData
    {
        std::string name;
        
        bool hasCustomLineColor;
        ImVec4 lineColor;
        
        std::vector<float> xData;
        std::vector<float> yData;
        float xMin = 0;
        float xMax = 1;
        float yMin = 0;
        float yMax = 1;
    };
    
    struct DataToAppend
    {
        ImGuiID group;
        float xValue;
        float yValue;
    };
    
    struct GroupToAdd
    {
        std::string name;
        std::string style;
    };
    
private:
    void parseAndFillStyle(const std::string& style, GroupData& group)
    {
        IM_ASSERT (style[0] == '#'); // only support format is #RRGGBB in hexidecimal, e.g. #ff000000 for red.
        int r, g, b, a;
        if (sscanf(style.c_str(), "#%02x%02x%02x%02x", &r, &g, &b, &a) == 4)
        {
            group.lineColor = ImVec4(r,g,b,a);
            group.hasCustomLineColor = true;
        }
        else
        {
            IM_ASSERT(false); // "Could not parse color string");
        }
    }
    
private:
    std::unordered_map<ImGuiID,GroupData> _groupData;
    std::vector<DataToAppend> _cacheOfDataToAppend;
        
    struct {
        std::mutex lock;
        std::vector<DataToAppend> dataSinceLastFrame;
        std::vector<GroupToAdd> addedGroupsSinceLastFrame;
        ImGuiStorage existingGroups;
    } concurrent;
    
    struct {
        float xMin = 0;
        float xMax = 1;
        float yMin = 0;
        float yMax = 1;
    } _dataBounds; // across all groups.
    
    bool _autoFitEnabled = true;
};

void AddPlotValue(const char* windowName,
                  const char* groupName,
                  double yValue,
                  double xValue,
                  const char* style)
{
    PlotWindow* plotWindow = FindWindow<PlotWindow> (windowName);
    
    // The window exists, just update the data.
    if (plotWindow)
    {
        plotWindow->AddPlotValue(groupName, yValue, xValue, style);
        return;
    }
    
    // Need to create it, enqueue that in the list of tasks for the next frame;
    {
        std::string windowNameCopy = windowName;
        std::string groupNameCopy = groupName;
        std::string styleCopy = style ? style : "";
        RunOnceInImGuiThread([windowNameCopy,groupNameCopy,xValue,yValue,styleCopy](){
            PlotWindow* plotWindow = FindOrCreateWindow<PlotWindow>(windowNameCopy.c_str());
            plotWindow->AddPlotValue(groupNameCopy.c_str(), yValue, xValue, styleCopy.empty() ? nullptr : styleCopy.c_str());
        });
    }
}

} // CVLog
} // ImGui
