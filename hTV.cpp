/*
 * Copyright 2026, Kris Beazley (ablyss) hTV@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */
 
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Unified state tracker containing both graphics backend slots
struct PlayerCtx {
    SDL_Window* window;
    SDL_GLContext glContext;   // Used if driver is present
    SDL_Renderer* renderer;    // Used if fallback software pipe runs
    SDL_Texture* texture;      // Used if fallback software pipe runs
    mpv_handle* mpv;
    mpv_render_context* mpvRender;
    bool isRunning;
    bool isFullscreen;
    char currentTitle[512]; 
    int texWidth, texHeight;
};

// Wake up the main loop on a new video frame arrival
void on_mpv_render_update(void* ctx) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_USEREVENT;
    SDL_PushEvent(&event);
}

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
    setenv("BE_APP_SIGNATURE", "application/x-vnd.hTV", 1);

    const char* streamUrl = "";
    if (argc > 1 && argv[1] != nullptr) {
        streamUrl = argv[1];
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL Could not initialize: %s\n", SDL_GetError());
        return 1;
    }

    // --- CHECK FOR MESA OPENGL DRIVER CAPABILITY AT RUNTIME ---
    struct stat mesaBuffer;
    bool hasHardwareDriver = (stat("/boot/system/add-ons/opengl/egl_vendor.d/libEGL_mesa.so", &mesaBuffer) == 0);

    PlayerCtx ctx;
    ctx.isRunning = true;
    ctx.isFullscreen = false;
    ctx.glContext = nullptr;
    ctx.renderer = nullptr;
    ctx.texture = nullptr;
    ctx.texWidth = 0;
    ctx.texHeight = 0;
    memset(ctx.currentTitle, 0, sizeof(ctx.currentTitle));
    snprintf(ctx.currentTitle, sizeof(ctx.currentTitle), "hTV Player");

    if (hasHardwareDriver) {
        printf("[DEBUG] Drivers detected. Requesting hardware OpenGL context...\n");
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    } else {
        printf("[DEBUG] Drivers missing. Initializing fallback 2D blit engine...\n");
    }

    ctx.window = SDL_CreateWindow(
        ctx.currentTitle,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        740, 520,
        (hasHardwareDriver ? SDL_WINDOW_OPENGL : 0) | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );

    if (!ctx.window) {
        fprintf(stderr, "Failed to create window wrapper: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Allocate graphics subsystems conditionally based on our runtime flag
    if (hasHardwareDriver) {
        ctx.glContext = SDL_GL_CreateContext(ctx.window);
        if (!ctx.glContext) {
            fprintf(stderr, "OpenGL Context creation failed: %s\nFallback to SW mode...\n", SDL_GetError());
            hasHardwareDriver = false; // Graceful fallback if creation error triggers
        } else {
            SDL_GL_MakeCurrent(ctx.window, ctx.glContext);
        }
    }

    if (!hasHardwareDriver) {
        ctx.renderer = SDL_CreateRenderer(ctx.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ctx.renderer) {
            ctx.renderer = SDL_CreateRenderer(ctx.window, -1, SDL_RENDERER_SOFTWARE);
        }
    }

    ctx.mpv = mpv_create();
    if (!ctx.mpv) {
        fprintf(stderr, "Failed to create mpv handle instance\n");
        return 1;
    }

    if (hasHardwareDriver) {
        mpv_set_option_string(ctx.mpv, "vo", "libmpv");
        mpv_set_option_string(ctx.mpv, "hwdec", "yes"); 
    } else {
        mpv_set_option_string(ctx.mpv, "vo", "libmpv");
        mpv_set_option_string(ctx.mpv, "profile", "sw-fast");
        mpv_set_option_string(ctx.mpv, "hwdec", "no"); 
    }

    mpv_set_option_string(ctx.mpv, "terminal", "no");
    mpv_set_option_string(ctx.mpv, "msg-level", "all=no");
    mpv_set_option_string(ctx.mpv, "osd-level", "1");

    mpv_set_option_string(ctx.mpv, "cache", "yes");               
    mpv_set_option_string(ctx.mpv, "demuxer-max-bytes", "200M");  
    mpv_set_option_string(ctx.mpv, "demuxer-max-back-bytes", "150M");
    mpv_set_option_string(ctx.mpv, "force-seekable", "yes");     
    mpv_set_option_string(ctx.mpv, "video-sync", "audio");
    mpv_set_option_string(ctx.mpv, "audio-pitch-correction", "no");

    if (mpv_initialize(ctx.mpv) < 0) {
        fprintf(stderr, "Failed to initialize mpv client core\n");
        return 1;
    }
    
    mpv_set_option_string(ctx.mpv, "speed", "1.05");

    // Dynamic Parameter Layout Configuration block
    mpv_opengl_init_params glParams;
    mpv_render_param* paramsPtr;

    mpv_render_param paramsHW[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glParams},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_param paramsSW[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_SW},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (hasHardwareDriver) {
        glParams.get_proc_address = [](void* ctx, const char* name) -> void* {
            return (void*)SDL_GL_GetProcAddress(name);
        };
        glParams.get_proc_address_ctx = nullptr;
        paramsPtr = paramsHW;
    } else {
        paramsPtr = paramsSW;
    }

    if (mpv_render_context_create(&ctx.mpvRender, ctx.mpv, paramsPtr) < 0) {
        fprintf(stderr, "Failed to bind mpv rendering frame pipeline\n");
        return 1;
    }

    mpv_render_context_set_update_callback(ctx.mpvRender, on_mpv_render_update, nullptr);

    printf("[DEBUG] Launching source feed stream target: %s\n", streamUrl);
    const char* loadCmd[] = {"loadfile", streamUrl, nullptr};
    mpv_command(ctx.mpv, loadCmd);

    SDL_Event event;
    uint32_t lastTitleUpdate = 0;
    bool needsRender = false; 

	{
	    const char* targetUrl = "https://raw.githubusercontent.com/ablyssx74/hTV/refs/heads/main/VERSION";
	    const char* localVersion = "v1.0.7"; 
	
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
        if (SDL_WaitEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: {
                    ctx.isRunning = false;
                    break;
                }
                case SDL_KEYDOWN: {
                    switch (event.key.keysym.sym) {
                        case SDLK_q: ctx.isRunning = false; break;                        
                        case SDLK_UP: {
                            mpv_command_string(ctx.mpv, "add volume 5");
                            mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                            break;
                        }
                        case SDLK_DOWN: {
                            mpv_command_string(ctx.mpv, "add volume -5");
                            mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                            break;
                        }
                        case SDLK_LEFTBRACKET: mpv_command_string(ctx.mpv, "add speed -0.01"); break;
                        case SDLK_RIGHTBRACKET: mpv_command_string(ctx.mpv, "add speed 0.01"); break;
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
                case SDL_MOUSEBUTTONDOWN: {
                    if (event.button.button == SDL_BUTTON_LEFT && event.button.clicks == 2) {
                        ctx.isFullscreen = !ctx.isFullscreen;
                        SDL_SetWindowFullscreen(ctx.window, ctx.isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_ShowCursor(ctx.isFullscreen ? SDL_DISABLE : SDL_ENABLE);
                    } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                        mpv_command_string(ctx.mpv, "cycle mute");
                    }
                    break;
                }
                case SDL_MOUSEWHEEL: {
                    if (event.wheel.y > 0) {
                        mpv_command_string(ctx.mpv, "add volume 5");
                        mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                    } else if (event.wheel.y < 0) {
                        mpv_command_string(ctx.mpv, "add volume -5");
                        mpv_command_string(ctx.mpv, "show-text \"Volume: ${volume}%\"");
                    }
                    break;
                }
                case SDL_DROPFILE: {
                    char* droppedFilePath = event.drop.file;
                    if (droppedFilePath != nullptr && droppedFilePath[0] != '\0') {
                        printf("[DEBUG] File received via Drag & Drop: %s\n", droppedFilePath);
                        const char* loadCmd[] = {"loadfile", droppedFilePath, nullptr};
                        mpv_command(ctx.mpv, loadCmd);
                        SDL_RaiseWindow(ctx.window);
                    }
                    SDL_free(droppedFilePath); 
                    break;
                }
                case SDL_USEREVENT: needsRender = true; break;
                case SDL_WINDOWEVENT: {
                    if (event.window.event == SDL_WINDOWEVENT_EXPOSED || 
                        event.window.event == SDL_WINDOWEVENT_RESIZED) {
                        needsRender = true;
                    }
                    break;
                }
            }
        }

        uint32_t currentTime = SDL_GetTicks();
        if (currentTime - lastTitleUpdate > 250) {
            UpdatePlayerWindowTitle(&ctx);
            lastTitleUpdate = currentTime;
        }
        
        // --- DYNAMIC RENDERING BLOCK ---
        if (needsRender && ctx.mpvRender) {
            int w, h;
            SDL_GetWindowSize(ctx.window, &w, &h);

            if (hasHardwareDriver) {
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
            } else {
                if (!ctx.texture || ctx.texWidth != w || ctx.texHeight != h) {
                    if (ctx.texture) SDL_DestroyTexture(ctx.texture);
                    ctx.texture = SDL_CreateTexture(
                        ctx.renderer, 
                        SDL_PIXELFORMAT_ARGB8888, 
                        SDL_TEXTUREACCESS_STREAMING, 
                        w, h
                    );
                    ctx.texWidth = w;
                    ctx.texHeight = h;
                }

                void* pixels = nullptr;
                int pitch = 0;
                
                if (SDL_LockTexture(ctx.texture, nullptr, &pixels, &pitch) == 0) {
                    int sizeParams[] = { w, h };
                    char fmtParams[] = "bgr0"; 

                    mpv_render_param renderParams[] = {
                        {MPV_RENDER_PARAM_SW_SIZE, sizeParams},
                        {MPV_RENDER_PARAM_SW_STRIDE, &pitch},
                        {MPV_RENDER_PARAM_SW_POINTER, pixels},
                        {MPV_RENDER_PARAM_SW_FORMAT, fmtParams}, 
                        {MPV_RENDER_PARAM_INVALID, nullptr}
                    };

                    mpv_render_context_render(ctx.mpvRender, renderParams);
                    SDL_UnlockTexture(ctx.texture);
                }

                SDL_RenderClear(ctx.renderer);
                SDL_RenderCopy(ctx.renderer, ctx.texture, nullptr, nullptr);
                SDL_RenderPresent(ctx.renderer);
            }
            needsRender = false; 
        }
    }

    if (ctx.texture) SDL_DestroyTexture(ctx.texture);
    if (ctx.renderer) SDL_DestroyRenderer(ctx.renderer);
    if (ctx.glContext) SDL_GL_DeleteContext(ctx.glContext);

    mpv_render_context_free(ctx.mpvRender);
    mpv_destroy(ctx.mpv);
    SDL_DestroyWindow(ctx.window);
    SDL_Quit();

    return 0;
}

