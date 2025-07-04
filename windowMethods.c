#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Your VM's internal headers
#include "windowMethods.h"

#include <assert.h>

#include "value.h"
#include "object.h"
#include "vm.h"
#include "memory.h"

static GlobalWindow gw = {0};  // One global SDL window


Value window_init(int argCount, Value* args) {
    if (gw.isInitialized) return NIL_VAL;  // Only one window

    int width = AS_NUMBER(args[0]);
    int height = AS_NUMBER(args[1]);
    const char* title = AS_STRING(args[2])->chars;

    SDL_Init(SDL_INIT_VIDEO);
    gw.window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
    gw.renderer = SDL_CreateRenderer(gw.window, -1, SDL_RENDERER_ACCELERATED);
    gw.isInitialized = true;


    return NIL_VAL;
}

Value window_clear(int argCount, Value* args) {
    int color = AS_NUMBER(args[0]);
    SDL_SetRenderDrawColor(gw.renderer, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 255);
    SDL_RenderClear(gw.renderer);
    return NIL_VAL;
}

Value window_drawRect(int argCount, Value* args) {
    int x = AS_NUMBER(args[0]);
    int y = AS_NUMBER(args[1]);
    int w = AS_NUMBER(args[2]);
    int h = AS_NUMBER(args[3]);
    int color = AS_NUMBER(args[4]);

    SDL_SetRenderDrawColor(gw.renderer, (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 255);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(gw.renderer, &rect);

    return NIL_VAL;
}

Value window_update(int argCount, Value* args) {
    SDL_RenderPresent(gw.renderer);
    return NIL_VAL;
}

Value window_pollEvent(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "pollEvent() takes no arguments.");
        return NIL_VAL;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return OBJ_VAL(copyString("quit", 4));

            case SDL_MOUSEBUTTONDOWN:
                switch (event.button.button) {
                case SDL_BUTTON_LEFT:   return OBJ_VAL(copyString("mouse_left", 10));
                case SDL_BUTTON_RIGHT:  return OBJ_VAL(copyString("mouse_right", 11));
                case SDL_BUTTON_MIDDLE: return OBJ_VAL(copyString("mouse_middle", 12));
                default:                return OBJ_VAL(copyString("mouse_default", 13));
                }

            case SDL_KEYDOWN:
                return OBJ_VAL(copyString("key_down", 7));

            case SDL_KEYUP:
                return OBJ_VAL(copyString("key_up", 5));

            default:
                break;
        }
    }

    return NIL_VAL;
}


Value window_getMousePosition(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "getMousePosition() takes no arguments."); return NIL_VAL;
    }

    int x, y;
    SDL_GetMouseState(&x, &y);
    ObjList* list = newList();

    writeValueArray(&list->elements, NUMBER_VAL(x));
    writeValueArray(&list->elements, NUMBER_VAL(y));

    return OBJ_VAL(list);
}

Value window_drawCircle(int argCount, Value* args) {
    if (argCount != 4 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
        !IS_NUMBER(args[2]) || !IS_NUMBER(args[3])) {
        runtimeError(vm.illegalArgumentsErrorClass, "drawCircle(x, y, radius, color) expects 4 number arguments.");
        return NIL_VAL;
        }

    int cx = (int)AS_NUMBER(args[0]);
    int cy = (int)AS_NUMBER(args[1]);
    int r  = (int)AS_NUMBER(args[2]);
    int color = (int)AS_NUMBER(args[3]);

    Uint8 red   = (color >> 16) & 0xFF;
    Uint8 green = (color >> 8) & 0xFF;
    Uint8 blue  = color & 0xFF;

    SDL_SetRenderDrawColor(gw.renderer, red, green, blue, 255);

    for (int w = 0; w < r * 2; w++) {
        for (int h = 0; h < r * 2; h++) {
            int dx = r - w;
            int dy = r - h;
            if ((dx * dx + dy * dy) <= (r * r)) {
                SDL_RenderDrawPoint(gw.renderer, cx + dx, cy + dy);
            }
        }
    }

    return NIL_VAL;
}

/*#include <SDL_image.h>
static SDL_Texture* loadTexture(const char* path) {
    SDL_Surface* surface = IMG_Load(path);
    if (!surface) {
        fprintf(stderr, "IMG_Load error: %s\n", IMG_GetError());
        return NULL;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(gw.renderer, surface);
    SDL_FreeSurface(surface);
    return texture;
}

Value window_drawImage(int argCount, Value* args) {
    if (argCount != 3 || !IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_STRING(args[2])) {
        runtimeError(vm.illegalArgumentsErrorClass, "drawImage(x, y, path) expects 2 numbers and a string.");
        return NIL_VAL;
    }

    int x = (int)AS_NUMBER(args[0]);
    int y = (int)AS_NUMBER(args[1]);
    ObjString* path = AS_STRING(args[2]);

    SDL_Texture* tex = loadTexture(path->chars);
    if (!tex) return NIL_VAL;

    int w, h;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(gw.renderer, tex, NULL, &dst);

    SDL_DestroyTexture(tex); // Optional: cache if reused
    return NIL_VAL;
}
*/