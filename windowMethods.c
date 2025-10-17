#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    gw.renderer = SDL_CreateRenderer(gw.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    gw.isInitialized = true;

    return NIL_VAL;
}

Value window_exit(int argCount, Value* args) {
    if (!gw.isInitialized) return NIL_VAL;  // No window to clean up

    SDL_DestroyRenderer(gw.renderer);
    SDL_DestroyWindow(gw.window);
    SDL_Quit();

    gw.window = NULL;
    gw.renderer = NULL;
    gw.isInitialized = false;

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

#include <SDL2_gfxPrimitives.h>

Value window_drawLine(int argCount, Value* args) {
    int x1 = AS_NUMBER(args[0]);
    int y1 = AS_NUMBER(args[1]);
    int x2 = AS_NUMBER(args[2]);
    int y2 = AS_NUMBER(args[3]);
    int color = AS_NUMBER(args[4]);

    int thickness = 1;
    if (argCount > 5)
        thickness = AS_NUMBER(args[5]);

    Uint8 r = (color >> 16) & 0xFF;
    Uint8 g = (color >> 8) & 0xFF;
    Uint8 b = color & 0xFF;

    GFX_thickLineRGBA(gw.renderer, x1, y1, x2, y2, thickness, r, g, b, 255);

    return NIL_VAL;
}

Value window_drawTriangle(int argCount, Value* args) {
    int x1 = AS_NUMBER(args[0]);
    int y1 = AS_NUMBER(args[1]);
    int x2 = AS_NUMBER(args[2]);
    int y2 = AS_NUMBER(args[3]);
    int x3 = AS_NUMBER(args[4]);
    int y3 = AS_NUMBER(args[5]);
    int color = AS_NUMBER(args[6]);

    Uint8 r = (color >> 16) & 0xFF;
    Uint8 g = (color >> 8) & 0xFF;
    Uint8 b = color & 0xFF;

    GFX_filledTrigonRGBA(gw.renderer, x1, y1, x2, y2, x3, y3, r, g, b, 255);

    return NIL_VAL;
}

#include <SDL2/SDL_ttf.h>

Value window_drawText(int argCount, Value* args) {

    if (argCount != 5) {
        runtimeError(vm.illegalArgumentsErrorClass, 
                     "drawText(x, y, text, size, color) expects 5 arguments.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_STRING(args[2]) ||
        !IS_NUMBER(args[3]) || !IS_NUMBER(args[4])) {
        runtimeError(vm.illegalArgumentsErrorClass, 
                     "drawText: argument types must be (number, number, string, number, number).");
        return NIL_VAL;
    }

    int x = (int)AS_NUMBER(args[0]);
    int y = (int)AS_NUMBER(args[1]);
    ObjString* textObj = AS_STRING(args[2]);
    int fontSize = (int)AS_NUMBER(args[3]);
    int sizeColor = (int)AS_NUMBER(args[4]); // last argument is color

    Uint8 r = (sizeColor >> 16) & 0xFF;
    Uint8 g = (sizeColor >> 8) & 0xFF;
    Uint8 b = sizeColor & 0xFF;

    // Initialize TTF if needed
    if (TTF_WasInit() == 0) {
        if (TTF_Init() == -1) {
            fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
            return NIL_VAL;
        }
    }

    // Default font path
    const char* defaultFontPath = "/usr/share/fonts/TTF/ZedMonoNerdFont-Regular.ttf";

    // Load font with user-specified size
    TTF_Font* font = TTF_OpenFont(defaultFontPath, fontSize);
    if (!font) {
        fprintf(stderr, "TTF_OpenFont failed: %s\n", TTF_GetError());
        return NIL_VAL;
    }

    SDL_Color color = { r, g, b, 255 };
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, textObj->chars, color);
    if (!surface) {
        TTF_CloseFont(font);
        fprintf(stderr, "TTF_RenderUTF8_Blended failed: %s\n", TTF_GetError());
        return NIL_VAL;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(gw.renderer, surface);
    SDL_Rect dst = { x, y, surface->w, surface->h };

    SDL_FreeSurface(surface);
    SDL_RenderCopy(gw.renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    TTF_CloseFont(font);

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

    ObjList* list = newList();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                writeValueArray(&list->elements, OBJ_VAL(copyString("quit", 4)));
                return OBJ_VAL(list);

            case SDL_MOUSEBUTTONDOWN:
                writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_down", 10)));
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT: writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_left", 10))); break;
                    case SDL_BUTTON_RIGHT:  writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_right", 11))); break;
                    case SDL_BUTTON_MIDDLE: writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_middle", 12))); break;
                    default:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_other", 14))); break;
                }
                return OBJ_VAL(list);

            case SDL_MOUSEBUTTONUP:
                writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_up", 8)));
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT: writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_left", 10))); break;
                    case SDL_BUTTON_RIGHT:  writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_right", 11))); break;
                    case SDL_BUTTON_MIDDLE: writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_middle", 12))); break;
                    default:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_other", 14))); break;
                }
                return OBJ_VAL(list);

            case SDL_KEYDOWN:
                writeValueArray(&list->elements, OBJ_VAL(copyString("key_down", 8)));
                writeValueArray(&list->elements, OBJ_VAL(copyString(SDL_GetKeyName(event.key.keysym.sym), strlen(SDL_GetKeyName(event.key.keysym.sym)))));
                return OBJ_VAL(list);

            case SDL_KEYUP:
                writeValueArray(&list->elements, OBJ_VAL(copyString("key_up", 6)));
                writeValueArray(&list->elements, OBJ_VAL(copyString(SDL_GetKeyName(event.key.keysym.sym), strlen(SDL_GetKeyName(event.key.keysym.sym)))));
                return OBJ_VAL(list);

            default:
                break;
        }
    }

    writeValueArray(&list->elements, OBJ_VAL(copyString("no_event", 8)));
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

#include <SDL_image.h>
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

Value window_loadImage(int argCount, Value* args) {
    if (argCount != 1 && argCount != 3) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "loadImage(path[, width, height]) expects 1 or 3 arguments.");
        return NIL_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm.illegalArgumentsErrorClass, "loadImage: first argument must be a string path.");
        return NIL_VAL;
    }

    ObjString* path = AS_STRING(args[0]);

    SDL_Texture* tex = loadTexture(path->chars);
    if (!tex) return NIL_VAL;

    int texW, texH;
    SDL_QueryTexture(tex, NULL, NULL, &texW, &texH);

    ObjImage* image = newImage(tex, texW, texH);

    if (argCount == 3) {
        if (!IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
            runtimeError(vm.illegalArgumentsErrorClass, "loadImage: width and height must be numbers.");
            return NIL_VAL;
        }
        image->width = (int)AS_NUMBER(args[1]);
        image->height = (int)AS_NUMBER(args[2]);
    }

    return OBJ_VAL(image);
}

Value window_drawImage(int argCount, Value* args) {
    if (argCount < 3 || argCount > 5) {
        runtimeError(vm.illegalArgumentsErrorClass, "drawImage(x, y, imageOrPath[, width, height]) expects 3–5 arguments.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError(vm.illegalArgumentsErrorClass, "drawImage: x and y must be numbers.");
        return NIL_VAL;
    }

    int x = (int)AS_NUMBER(args[0]);
    int y = (int)AS_NUMBER(args[1]);

    SDL_Texture* tex = NULL;
    int w, h;

    if (IS_STRING(args[2])) {
        ObjString* path = AS_STRING(args[2]);
        tex = loadTexture(path->chars);
        if (!tex) return NIL_VAL;
        SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    } else if (IS_OBJ(args[2]) && AS_OBJ(args[2])->type == OBJ_IMAGE) {
        ObjImage* image = (ObjImage*)AS_OBJ(args[2]);
        tex = image->texture;
        w = image->width;
        h = image->height;
    } else {
        runtimeError(vm.illegalArgumentsErrorClass, "drawImage: third argument must be string or Image.");
        return NIL_VAL;
    }

    if (argCount >= 5) {
        if (!IS_NUMBER(args[3]) || !IS_NUMBER(args[4])) {
            runtimeError(vm.illegalArgumentsErrorClass, "drawImage: width and height must be numbers.");
            return NIL_VAL;
        }
        w = (int)AS_NUMBER(args[3]);
        h = (int)AS_NUMBER(args[4]);
    }

    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(gw.renderer, tex, NULL, &dst);

    if (IS_STRING(args[2])) SDL_DestroyTexture(tex); // Don't destroy shared image

    return NIL_VAL;
}

Value Image_getWidth(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "getWidth() takes no arguments.");
        return NIL_VAL;
    }

    ObjImage* image = AS_IMAGE(args[-1]);
    return NUMBER_VAL(image->width);
}

Value Image_getHeight(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "getWidth() takes no arguments.");
        return NIL_VAL;
    }

    ObjImage* image = AS_IMAGE(args[-1]);
    return NUMBER_VAL(image->height);
}
