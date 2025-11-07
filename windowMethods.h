#ifndef GEM_WINDOW_METHODS_H
#define GEM_WINDOW_METHODS_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// Forward declarations of your language's value system
#include "value.h"

// Structure representing the global SDL window and renderer state
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    bool isInitialized;
} GlobalWindow;

// Initializes the SDL window with width, height, and title
Value window_init(Thread* ctx, int argCount, Value* args);

// Clears the window with the specified RGB color (given as a 0xRRGGBB int)
Value window_clear(Thread* ctx, int argCount, Value* args);

// Draws a filled rectangle at (x, y) with width w, height h, and fill color (0xRRGGBB)
Value window_drawRect(Thread* ctx, int argCount, Value* args);

// Presents the rendered frame to the screen
Value window_update(Thread* ctx, int argCount, Value* args);

// Polls for SDL events and returns "quit" string if SDL_QUIT is triggered
Value window_pollEvent(Thread* ctx, int argCount, Value* args);


Value window_drawCircle(Thread* ctx, int argCount, Value* args);
Value window_drawLine(Thread* ctx, int argCount, Value* args);
Value window_drawTriangle(Thread* ctx, int argCount, Value* args);
Value window_drawImage(Thread* ctx, int argCount, Value* args);
Value window_getMousePosition(Thread* ctx, int argCount, Value* args);
Value window_loadImage(Thread* ctx, int argCount, Value* args);
Value Image_getHeight(Thread* ctx, int argCount, Value* args);
Value Image_getWidth(Thread* ctx, int argCount, Value* args);
Value window_exit(Thread* ctx, int argCount, Value* args);
Value window_drawText(Thread* ctx, int argCount, Value* args);
#endif // GEM_WINDOW_METHODS_H
