/*
 * Copyright 2026, Kris Beazley Cricket@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>





// Global structures for player state tracking
struct PlayerCtx {
    SDL_Window* window;
    SDL_GLContext glContext;
    mpv_handle* mpv;
    mpv_render_context* mpvRender;
    bool isRunning;
    bool isFullscreen;
    char currentTitle[512]; 
};


// Callback to wake up the SDL main thread loop when a new frame is ready
void on_mpv_render_update(void* ctx) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_USEREVENT;
    SDL_PushEvent(&event);
}

// Function to dynamically look up media status and update the window frame title
void UpdatePlayerWindowTitle(PlayerCtx* ctx) {
    if (!ctx->mpv || !ctx->window) return;

    char* titleStr = nullptr;
    mpv_get_property(ctx->mpv, "media-title", MPV_FORMAT_STRING, &titleStr);

    int isBuffering = 0;
    int64_t bufferPercent = 0;
    mpv_get_property(ctx->mpv, "paused-for-cache", MPV_FORMAT_FLAG, &isBuffering);
    mpv_get_property(ctx->mpv, "cache-buffering-state", MPV_FORMAT_INT64, &bufferPercent);

    const char* activeName = "Ready to Stream";
    
    char* pathStr = nullptr;
    mpv_get_property(ctx->mpv, "path", MPV_FORMAT_STRING, &pathStr);

    if (pathStr == nullptr || pathStr[0] == '\0') {
        activeName = "Idle";
    } else if (titleStr != nullptr && titleStr[0] != '\0') { 
        activeName = titleStr;
    } else {
        activeName = pathStr; 
    }

    char finalTitle[512];
    if (isBuffering && pathStr != nullptr && pathStr[0] != '\0') {
        snprintf(finalTitle, sizeof(finalTitle), "hTV - [Buffering %d%%] - %s", (int)bufferPercent, activeName);
    } else {
        snprintf(finalTitle, sizeof(finalTitle), "hTV - %s", activeName);
    }

    if (strcmp(ctx->currentTitle, finalTitle) != 0) {
        snprintf(ctx->currentTitle, sizeof(ctx->currentTitle), "%s", finalTitle);
        SDL_SetWindowTitle(ctx->window, ctx->currentTitle);
    }

    if (titleStr) mpv_free(titleStr);
    if (pathStr) mpv_free(pathStr);
}

int main(int argc, char* argv[]) {
    setenv("BE_APP_SIGNATURE", "application/x-vnd.HaikuStreamPlayer", 1);

    const char* streamUrl = "";
    if (argc > 1 && argv[1] != nullptr) {
        streamUrl = argv[1];
    }


    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL Could not initialize: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(NULL, NULL);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    PlayerCtx ctx;
    ctx.isRunning = true;
    ctx.isFullscreen = false;
    memset(ctx.currentTitle, 0, sizeof(ctx.currentTitle));
    snprintf(ctx.currentTitle, sizeof(ctx.currentTitle), "hTV Player");

    ctx.window = SDL_CreateWindow(
        ctx.currentTitle,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        740, 520,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );

    if (!ctx.window) {
        fprintf(stderr, "Failed to create window wrapper: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    ctx.glContext = SDL_GL_CreateContext(ctx.window);
    if (!ctx.glContext) {
        fprintf(stderr, "OpenGL Context creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx.window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(ctx.window, ctx.glContext);

    ctx.mpv = mpv_create();
    if (!ctx.mpv) {
        fprintf(stderr, "Failed to create mpv handle instance\n");
        return 1;
    }

    // Quieted configurations ensuring terminal stays clean
    mpv_set_option_string(ctx.mpv, "vo", "libmpv");
    mpv_set_option_string(ctx.mpv, "hwdec", "yes"); 
    mpv_set_option_string(ctx.mpv, "terminal", "no");
    mpv_set_option_string(ctx.mpv, "msg-level", "all=no");
    mpv_set_option_string(ctx.mpv, "osd-level", "1");

    // --- ENFORCE LIVE STREAM TIMELINE CACHING (SCROLL BACK IN TIME) ---
    mpv_set_option_string(ctx.mpv, "cache", "yes");               
    mpv_set_option_string(ctx.mpv, "demuxer-max-bytes", "200M");  
    mpv_set_option_string(ctx.mpv, "demuxer-max-back-bytes", "150M");
    mpv_set_option_string(ctx.mpv, "force-seekable", "yes");     

    // Force mpv to drop video frames to keep audio pitch completely native
    mpv_set_option_string(ctx.mpv, "video-sync", "audio");

    mpv_set_option_string(ctx.mpv, "audio-pitch-correction", "no");


    if (mpv_initialize(ctx.mpv) < 0) {
        fprintf(stderr, "Failed to initialize mpv client layout core\n");
        return 1;
    }
    
   // Initialize playback speed to the perfect 1.05 audio baseline pitch modification
    //double initialSpeed = 1.05;
    mpv_set_option_string(ctx.mpv, "speed", "1.05");

    mpv_opengl_init_params glParams{
        [](void* ctx, const char* name) -> void* {
            return (void*)SDL_GL_GetProcAddress(name);
        },
        nullptr
    };

     mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&ctx.mpvRender, ctx.mpv, params) < 0) {
        fprintf(stderr, "Failed to bind mpv OpenGL rendering frame pipeline\n");
        return 1;
    }

    // Connect the frame arrival notification to the main thread event handler
    mpv_render_context_set_update_callback(ctx.mpvRender, on_mpv_render_update, nullptr);

    printf("[DEBUG] Launching source feed stream target: %s\n", streamUrl);
    const char* loadCmd[] = {"loadfile", streamUrl, nullptr};
    mpv_command(ctx.mpv, loadCmd);

    SDL_Event event;
    uint32_t lastTitleUpdate = 0;
    bool needsRender = false; // Tracks if a fresh frame actually arrived


	{
	    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/hTV/refs/heads/main/VERSION";
	    const char* localVersion = "v1.0.3"; 
	
	    char updateCmd[1024];
	    snprintf(updateCmd, sizeof(updateCmd),
	        "(REMOTE_V=$(curl -sL \"%s\" | tr -d '\\r\\n'); "
	        "if [ ! -z \"$REMOTE_V\" ] && [ \"$REMOTE_V\" != \"%s\" ]; then "
	        "notify --title \"Update Available\" --group \"hTV\" "
	        "\"A newer version of hTV is available! ($REMOTE_V)\"; fi) &",
	        targetUrl, localVersion);	
	    system(updateCmd);
	}

    while (ctx.isRunning) {
        // Wait indefinitely for any incoming event to drop idle CPU usage to 0%
        // This isolates mouse tracking states from getting stuck behind backbuffer swaps
        if (SDL_WaitEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: {
                    ctx.isRunning = false;
                    break;
                }
                
                // 1. Keyboard Inputs Case Block
                case SDL_KEYDOWN: {
                    switch (event.key.keysym.sym) {
                        case SDLK_q: {
                            ctx.isRunning = false;
                            break;
                        }                        
                        
                        case SDLK_UP: {
                            mpv_command_string(ctx.mpv, "add volume 5");
                            break;
                        }
                        case SDLK_DOWN: {
                            mpv_command_string(ctx.mpv, "add volume -5");
                            break;
                        }
                        // --- MULTIMEDIA TIMING DRIFT CORRECTION SHORTCUTS ---
                        case SDLK_LEFTBRACKET: {
                            // Subtly slow down audio speed (lowers pitch)
                            mpv_command_string(ctx.mpv, "add speed -0.01");
                            break;
                        }
                        case SDLK_RIGHTBRACKET: {
                            // Subtly speed up audio speed (raises pitch / sharpens vocals)
                            mpv_command_string(ctx.mpv, "add speed 0.01");
                            break;
                        }


                        case SDLK_f: {
                            ctx.isFullscreen = !ctx.isFullscreen;
                            SDL_SetWindowFullscreen(ctx.window, ctx.isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                            SDL_ShowCursor(ctx.isFullscreen ? SDL_DISABLE : SDL_ENABLE);
                            break;
                        }
                        case SDLK_SPACE: {
                            int pauseState = 0;
                            mpv_get_property(ctx.mpv, "pause", MPV_FORMAT_FLAG, &pauseState);
                            pauseState = !pauseState;
                            mpv_set_property(ctx.mpv, "pause", MPV_FORMAT_FLAG, &pauseState);
                            break;
                        }
                         case SDLK_LEFT: {
                            mpv_command_string(ctx.mpv, "seek -5 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }
                        case SDLK_RIGHT: {
                            mpv_command_string(ctx.mpv, "seek 5 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }
                        case SDLK_COMMA: {
                            mpv_command_string(ctx.mpv, "seek -10 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }
                        case SDLK_PERIOD: {
                            mpv_command_string(ctx.mpv, "seek 10 relative exact");
                            mpv_command_string(ctx.mpv, "show-progress");
                            break;
                        }


                    }
                    break;
                }


                // 2. Mouse Clicks Case Block
                case SDL_MOUSEBUTTONDOWN: {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        // Double Left Click toggles Fullscreen
                        if (event.button.clicks == 2) {
                            ctx.isFullscreen = !ctx.isFullscreen;
                            SDL_SetWindowFullscreen(ctx.window, ctx.isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                            SDL_ShowCursor(ctx.isFullscreen ? SDL_DISABLE : SDL_ENABLE);
                        }
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        // Single Middle Click toggles Mute with an OSD overlay
                        mpv_command_string(ctx.mpv, "cycle mute");
                    }
                    break;
                }

                // 3. Mouse Scroll Wheel Case Block
                case SDL_MOUSEWHEEL: {
                    if (event.wheel.y > 0) {
                        mpv_command_string(ctx.mpv, "add volume 5");
                    } else if (event.wheel.y < 0) {
                        mpv_command_string(ctx.mpv, "add volume -5");
                    }
                    break;
                }

                case SDL_DROPFILE: {
                    char* droppedFilePath = event.drop.file;
                    if (droppedFilePath != nullptr && droppedFilePath[0] != '\0') {
                        printf("[DEBUG] File received via Drag & Drop/Tracker: %s\n", droppedFilePath);
                        
                        // Pass the absolute path directly to libmpv
                        const char* loadCmd[] = {"loadfile", droppedFilePath, nullptr};
                        mpv_command(ctx.mpv, loadCmd);

                        // --- NEW NATIVE POLISH MATCH ---
                        // Force the application window to jump to the front and grab keyboard focus
                        SDL_RaiseWindow(ctx.window);
                    }
                    
                    SDL_free(droppedFilePath); // Safe memory cleanup
                    break;
                }


                // 4. Custom User Event - Fired by on_mpv_render_update
                case SDL_USEREVENT: {

                    // A new video matrix frame is officially decoded and waiting
                    needsRender = true;
                    break;
                }
                

                // 5. Window Mutations Case Block
                case SDL_WINDOWEVENT: {
                    if (event.window.event == SDL_WINDOWEVENT_EXPOSED || 
                        event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        needsRender = true;
                    }
                    break;
                }
            }
        }

        // Periodically refresh the window title metadata every 250ms
        uint32_t currentTime = SDL_GetTicks();
        if (currentTime - lastTitleUpdate > 250) {
            UpdatePlayerWindowTitle(&ctx);
            lastTitleUpdate = currentTime;
        }
        


        // --- OPTIMIZED ATOMIC RENDER PASS ---
        // We ONLY execute graphics driver operations if libmpv signaled an active update flag.
        if (needsRender && ctx.mpvRender) {
            int w, h;
            SDL_GetWindowSize(ctx.window, &w, &h);

            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            mpv_opengl_fbo fbo{ 0, w, h, 0 };
            int flip_y = 1;

            mpv_render_param renderParams[] = {
                {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
                {MPV_RENDER_PARAM_INVALID, nullptr}
            };

            mpv_render_context_render(ctx.mpvRender, renderParams);
            SDL_GL_SwapWindow(ctx.window);
            
            needsRender = false; // Reset rendering request tracking token
        }
    }

    mpv_render_context_free(ctx.mpvRender);
    mpv_destroy(ctx.mpv);
    SDL_GL_DeleteContext(ctx.glContext);
    SDL_DestroyWindow(ctx.window);
    SDL_Quit();

    return 0;
}
