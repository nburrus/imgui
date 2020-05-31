//
//  main.cpp
//  ImguiLogger
//
//  Created by Nicolas Burrus on 23/05/2020.
//  Copyright © 2020 Nicolas Burrus. All rights reserved.
//

// dear imgui: standalone example application for OSX + OpenGL2, using legacy fixed pipeline
// If you are new to dear imgui, see examples/README.txt and documentation at the top of imgui.cpp.

#include "imgui_logger.h"
#include "imgui_logger_view.h"
#include "imgui.h"

#include <thread>

//-----------------------------------------------------------------------------------
// ImguiLoggerAppDelegate
//-----------------------------------------------------------------------------------

@interface ImguiLoggerAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, readonly) NSWindow* window;
@end

@implementation ImguiLoggerAppDelegate
@synthesize window = _window;

-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication
{
    return YES;
}

-(NSWindow*)window
{
    if (_window != nil)
        return (_window);

    NSRect viewRect = NSMakeRect(100.0, 100.0, 100.0 + 1280.0, 100 + 720.0);

    _window = [[NSWindow alloc] initWithContentRect:viewRect styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:YES];
    [_window setTitle:@"ImGui::Logger Example"];
    [_window setAcceptsMouseMovedEvents:YES];
    [_window setOpaque:YES];
    [_window makeKeyAndOrderFront:NSApp];

    return (_window);
}

-(void)setupMenu
{
    NSMenu* mainMenuBar = [[NSMenu alloc] init];
    NSMenu* appMenu;
    NSMenuItem* menuItem;

    appMenu = [[NSMenu alloc] initWithTitle:@"Imgui Logger"];
    menuItem = [appMenu addItemWithTitle:@"Imgui Logger" action:@selector(terminate:) keyEquivalent:@"q"];
    [menuItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];

    menuItem = [[NSMenuItem alloc] init];
    [menuItem setSubmenu:appMenu];

    [mainMenuBar addItem:menuItem];

    appMenu = nil;
    [NSApp setMainMenu:mainMenuBar];
}

-(void)dealloc
{
    _window = nil;
}

void workerThread1()
{
    int i = 0;
    while (true)
    {
        auto imagePtr = std::make_shared<ImGui::Logger::Image>();
        imagePtr->width = 640;
        imagePtr->height = 480;
        imagePtr->bytesPerRow = imagePtr->width;
        imagePtr->data.resize (imagePtr->bytesPerRow * imagePtr->height);
        for (int r = 0; r < imagePtr->height; ++r)
        for (int c = 0; c < imagePtr->width; ++c)
        {
            const int idx = r*imagePtr->bytesPerRow + c;
            imagePtr->data[idx] = (c+r+i*i)%255;
        }
        
        ImGui::Logger::UpdateImage("VGAImage", imagePtr);
        
        for (int k = 0; k < 10; ++k)
        {
            ImGui::Logger::AddPlotValue(("PlotN - " + std::to_string(k)).c_str(), "Live", log(i+1+k), i);
            ImGui::Logger::AddPlotValue(("PlotN - " + std::to_string(k)).c_str(), "GT", log(i+1+k)/2.f, i);
        }
        
        ++i;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

#define IdentityMacro(Code)

void workerThread2()
{
    int offset = 0; // could use atomic, but we don't care for quick&dirty tests.
    ImGui::Logger::SetWindowRenderExtraCallback("SmallImage", "ModifyOffset", [&offset]() {
        ImGui::SliderInt("Adjust offset", &offset, 0, 320);
    });
    
    int i = 0;
    while (true)
    {
        auto imagePtr = std::make_shared<ImGui::Logger::Image>();
        imagePtr->width = 320;
        imagePtr->height = 240;
        imagePtr->bytesPerRow = imagePtr->width;
        imagePtr->data.resize (imagePtr->bytesPerRow * imagePtr->height);
        for (int r = 0; r < imagePtr->height; ++r)
        for (int c = 0; c < imagePtr->width; ++c)
        {
            const int idx = r*imagePtr->bytesPerRow + c;
            imagePtr->data[idx] = (c+r+offset)%255;
        }
        
        ImGui::Logger::UpdateImage("SmallImage", imagePtr);
        
        ImGui::Logger::AddPlotValue("Plot1", "Live", log(i*i + 1), i);
        ImGui::Logger::AddPlotValue("Plot1", "GT", log(i*i + 1) + 1, i);
        
        ++i;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    
    ImGui::Logger::SetWindowRenderExtraCallback("SmallImage", "ModifyOffset", nullptr);
}

-(void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    // Make the application a foreground application (else it won't receive keyboard events)
    ProcessSerialNumber psn = {0, kCurrentProcess};
    TransformProcessType(&psn, kProcessTransformToForegroundApplication);

    // Menu
    [self setupMenu];
    
    ImGui::Logger::Init (self.window);
    
    new std::thread([]() {
        workerThread1();
    });
    
    new std::thread([]() {
        workerThread2();
    });
}

@end

int main(int argc, const char* argv[])
{
    @autoreleasepool
    {
        NSApp = [NSApplication sharedApplication];
        ImguiLoggerAppDelegate* delegate = [[ImguiLoggerAppDelegate alloc] init];
        [[NSApplication sharedApplication] setDelegate:delegate];
        [NSApp run];
    }
    return NSApplicationMain(argc, argv);
}
