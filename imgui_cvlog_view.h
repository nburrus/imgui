//
//  imgui_cvlog_view.hpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 24/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

#pragma once

#import <Cocoa/Cocoa.h>

@interface ImguiLoggerView : NSOpenGLView
{
    NSTimer*    animationTimer;
}
@end

namespace ImGui
{
namespace CVLog
{

void Init(NSWindow* window);

}
}
