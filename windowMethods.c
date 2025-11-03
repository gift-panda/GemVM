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

char* getValueTypeName(Value value);

static GlobalWindow gw = {0};  // One global SDL window


Value window_init(int argCount, Value* args) {
    if (argCount != 3) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method init for arity %d.", argCount);
        return NIL_VAL;
    }

    if (gw.isInitialized) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "init() can only be called once — window already initialized.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) || !IS_STRING(args[2])) {
        runtimeError(vm.typeErrorClass,
                     "init(width, height, title) expected (Number, Number, String), but got (%s, %s, %s).",
                     getValueTypeName(args[0]),
                     getValueTypeName(args[1]),
                     getValueTypeName(args[2]));
         return NIL_VAL;
    }

    int width = (int)AS_NUMBER(args[0]);
    int height = (int)AS_NUMBER(args[1]);
    const char* title = AS_STRING(args[2])->chars;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        runtimeError(vm.formatErrorClass, "ray init failed: %s", SDL_GetError());
        return NIL_VAL;
    }

    gw.window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
    if (!gw.window) {
        runtimeError(vm.formatErrorClass, "window creation failed: %s", SDL_GetError());
        return NIL_VAL;
    }

    gw.renderer = SDL_CreateRenderer(gw.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!gw.renderer) {
        runtimeError(vm.formatErrorClass, "renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(gw.window);
        gw.window = NULL;
        return NIL_VAL;
    }

    gw.isInitialized = true;
    return NIL_VAL;
}

Value window_exit(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method exit for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!gw.isInitialized) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "exit() called before init() — no active window to close.");
        return NIL_VAL;
    }

    SDL_DestroyRenderer(gw.renderer);
    SDL_DestroyWindow(gw.window);
    SDL_Quit();

    gw.window = NULL;
    gw.renderer = NULL;
    gw.isInitialized = false;

    return NIL_VAL;
}

Value window_clear(int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method clear for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!gw.isInitialized) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "clear() called before init() — no active window to draw on.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm.typeErrorClass,
                     "clear(color) expected (Number), but got (%s).",
                     getValueTypeName(args[0]));
        return NIL_VAL;
    }

    int color = (int)AS_NUMBER(args[0]);
    SDL_SetRenderDrawColor(gw.renderer,
                           (color >> 16) & 0xFF,
                           (color >> 8) & 0xFF,
                           color & 0xFF,
                           255);
    SDL_RenderClear(gw.renderer);

    return NIL_VAL;
}

Value window_drawRect(int argCount, Value* args) {
    if (argCount != 5) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method drawRect for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!gw.isInitialized) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "drawRect() called before init() — no active window to draw on.");
        return NIL_VAL;
    }
    
    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
        !IS_NUMBER(args[2]) || !IS_NUMBER(args[3]) ||
        !IS_NUMBER(args[4])) {
        runtimeError(vm.typeErrorClass,
                     "drawRect(x, y, width, height, color) expected (Number, Number, Number, Number, Number), "
                     "but got (%s, %s, %s, %s, %s).",
                     getValueTypeName(args[0]),
                     getValueTypeName(args[1]),
                     getValueTypeName(args[2]),
                     getValueTypeName(args[3]),
                     getValueTypeName(args[4]));
        return NIL_VAL;
    }

    int x = (int)AS_NUMBER(args[0]);
    int y = (int)AS_NUMBER(args[1]);
    int w = (int)AS_NUMBER(args[2]);
    int h = (int)AS_NUMBER(args[3]);
    int color = (int)AS_NUMBER(args[4]);

    SDL_SetRenderDrawColor(gw.renderer,
                           (color >> 16) & 0xFF,
                           (color >> 8) & 0xFF,
                           color & 0xFF,
                           255);

    SDL_Rect rect = {x, y, w, h};
    if (SDL_RenderFillRect(gw.renderer, &rect) != 0) {
        runtimeError(vm.formatErrorClass, "drawRect failed: %s", SDL_GetError());
        return NIL_VAL;
    }

    return NIL_VAL;
}


#include <SDL2_gfxPrimitives.h>

Value window_drawLine(int argCount, Value* args) {
    if (argCount != 5 && argCount != 6) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method drawLine for arity %d.", argCount);
        return NIL_VAL;
    }
    
    if (!gw.isInitialized) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "drawLine() called before init() — no active window to draw on.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
        !IS_NUMBER(args[2]) || !IS_NUMBER(args[3]) ||
        !IS_NUMBER(args[4]) || (argCount == 6 && !IS_NUMBER(args[5]))) {
        if (argCount == 6) {
            runtimeError(vm.typeErrorClass,
                         "drawLine(x1, y1, x2, y2, color, thickness) expected (Number, Number, Number, Number, Number, Number), "
                         "but got (%s, %s, %s, %s, %s, %s).",
                         getValueTypeName(args[0]),
                         getValueTypeName(args[1]),
                         getValueTypeName(args[2]),
                         getValueTypeName(args[3]),
                         getValueTypeName(args[4]),
                         getValueTypeName(args[5]));
        } else {
            runtimeError(vm.typeErrorClass,
                         "drawLine(x1, y1, x2, y2, color) expected (Number, Number, Number, Number, Number), "
                         "but got (%s, %s, %s, %s, %s).",
                         getValueTypeName(args[0]),
                         getValueTypeName(args[1]),
                         getValueTypeName(args[2]),
                         getValueTypeName(args[3]),
                         getValueTypeName(args[4]));
        }
        return NIL_VAL;
    }

    int x1 = (int)AS_NUMBER(args[0]);
    int y1 = (int)AS_NUMBER(args[1]);
    int x2 = (int)AS_NUMBER(args[2]);
    int y2 = (int)AS_NUMBER(args[3]);
    int color = (int)AS_NUMBER(args[4]);
    int thickness = (argCount == 6) ? (int)AS_NUMBER(args[5]) : 1;

    Uint8 r = (color >> 16) & 0xFF;
    Uint8 g = (color >> 8) & 0xFF;
    Uint8 b = color & 0xFF;

    if (GFX_thickLineRGBA(gw.renderer, x1, y1, x2, y2, thickness, r, g, b, 255) != 0) {
        runtimeError(vm.formatErrorClass, "drawLine failed: %s", SDL_GetError());
        return NIL_VAL;
    }

    return NIL_VAL;
}

Value window_drawTriangle(int argCount, Value* args) {
    if (argCount != 7) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method drawTriangle for arity %d.", argCount);
        return NIL_VAL;
    }
    
    if (!gw.isInitialized) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "drawTriangle() called before init() — no active window to draw on.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
        !IS_NUMBER(args[2]) || !IS_NUMBER(args[3]) ||
        !IS_NUMBER(args[4]) || !IS_NUMBER(args[5]) ||
        !IS_NUMBER(args[6])) {
        runtimeError(vm.typeErrorClass,
                     "drawTriangle(x1, y1, x2, y2, x3, y3, color) expected "
                     "(Number, Number, Number, Number, Number, Number, Number), "
                     "but got (%s, %s, %s, %s, %s, %s, %s).",
                     getValueTypeName(args[0]),
                     getValueTypeName(args[1]),
                     getValueTypeName(args[2]),
                     getValueTypeName(args[3]),
                     getValueTypeName(args[4]),
                     getValueTypeName(args[5]),
                     getValueTypeName(args[6]));
        return NIL_VAL;
    }

    int x1 = (int)AS_NUMBER(args[0]);
    int y1 = (int)AS_NUMBER(args[1]);
    int x2 = (int)AS_NUMBER(args[2]);
    int y2 = (int)AS_NUMBER(args[3]);
    int x3 = (int)AS_NUMBER(args[4]);
    int y3 = (int)AS_NUMBER(args[5]);
    int color = (int)AS_NUMBER(args[6]);

    Uint8 r = (color >> 16) & 0xFF;
    Uint8 g = (color >> 8) & 0xFF;
    Uint8 b = color & 0xFF;

    if (GFX_filledTrigonRGBA(gw.renderer, x1, y1, x2, y2, x3, y3, r, g, b, 255) != 0) {
        runtimeError(vm.formatErrorClass, "drawTriangle failed: %s", SDL_GetError());
        return NIL_VAL;
    }

    return NIL_VAL;
}

#include <SDL2/SDL_ttf.h>

Value window_drawText(int argCount, Value* args) {
    if (argCount != 5) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method drawText for arity %d.", argCount);
        return NIL_VAL;
    }
    
    if (!gw.isInitialized) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "drawText() called before init() — no active window to draw on.");
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
        !IS_STRING(args[2]) || !IS_NUMBER(args[3]) || !IS_NUMBER(args[4])) {
        runtimeError(vm.typeErrorClass,
                     "drawText(x, y, text, size, color) expected (Number, Number, String, Number, Number), "
                     "but got (%s, %s, %s, %s, %s).",
                     getValueTypeName(args[0]),
                     getValueTypeName(args[1]),
                     getValueTypeName(args[2]),
                     getValueTypeName(args[3]),
                     getValueTypeName(args[4]));
        return NIL_VAL;
    }

    int x = (int)AS_NUMBER(args[0]);
    int y = (int)AS_NUMBER(args[1]);
    ObjString* textObj = AS_STRING(args[2]);
    int fontSize = (int)AS_NUMBER(args[3]);
    int colorVal = (int)AS_NUMBER(args[4]);

    Uint8 r = (colorVal >> 16) & 0xFF;
    Uint8 g = (colorVal >> 8) & 0xFF;
    Uint8 b = colorVal & 0xFF;

    if (TTF_WasInit() == 0 && TTF_Init() == -1) {
        runtimeError(vm.formatErrorClass, "text system init failed: %s", TTF_GetError());
        return NIL_VAL;
    }

    const char* defaultFontPath = "/usr/share/fonts/TTF/ZedMonoNerdFont-Regular.ttf";
    TTF_Font* font = TTF_OpenFont(defaultFontPath, fontSize);
    if (!font) {
        runtimeError(vm.formatErrorClass, "font load failed: %s", TTF_GetError());
        return NIL_VAL;
    }

    SDL_Color color = { r, g, b, 255 };
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, textObj->chars, color);
    if (!surface) {
        TTF_CloseFont(font);
        runtimeError(vm.formatErrorClass, "text render failed: %s", TTF_GetError());
        return NIL_VAL;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(gw.renderer, surface);
    if (!texture) {
        SDL_FreeSurface(surface);
        TTF_CloseFont(font);
        runtimeError(vm.formatErrorClass, "text texture creation failed: %s", SDL_GetError());
        return NIL_VAL;
    }

    SDL_Rect dst = { x, y, surface->w, surface->h };
    SDL_FreeSurface(surface);

    if (SDL_RenderCopy(gw.renderer, texture, NULL, &dst) != 0) {
        SDL_DestroyTexture(texture);
        TTF_CloseFont(font);
        runtimeError(vm.formatErrorClass, "text draw failed: %s", SDL_GetError());
        return NIL_VAL;
    }

    SDL_DestroyTexture(texture);
    TTF_CloseFont(font);

    return NIL_VAL;
}

Value window_update(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method update for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!gw.renderer) {
        runtimeError(vm.errorClass, "Renderer failed.");
        return NIL_VAL;
    }

    SDL_RenderPresent(gw.renderer);
    return NIL_VAL;
}

Value window_pollEvent(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "No method pollEvent for arity %d.", argCount);
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
                    case SDL_BUTTON_LEFT:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_left", 10))); break;
                    case SDL_BUTTON_RIGHT:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_right", 11))); break;
                    case SDL_BUTTON_MIDDLE:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_middle", 12))); break;
                    default:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_other", 11))); break;
                }
                return OBJ_VAL(list);

            case SDL_MOUSEBUTTONUP:
                writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_up", 8)));
                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_left", 10))); break;
                    case SDL_BUTTON_RIGHT:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_right", 11))); break;
                    case SDL_BUTTON_MIDDLE:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_middle", 12))); break;
                    default:
                        writeValueArray(&list->elements, OBJ_VAL(copyString("mouse_other", 11))); break;
                }
                return OBJ_VAL(list);

            case SDL_KEYDOWN:
                writeValueArray(&list->elements, OBJ_VAL(copyString("key_down", 8)));
                writeValueArray(&list->elements,
                    OBJ_VAL(copyString(SDL_GetKeyName(event.key.keysym.sym),
                    strlen(SDL_GetKeyName(event.key.keysym.sym)))));
                return OBJ_VAL(list);

            case SDL_KEYUP:
                writeValueArray(&list->elements, OBJ_VAL(copyString("key_up", 6)));
                writeValueArray(&list->elements,
                    OBJ_VAL(copyString(SDL_GetKeyName(event.key.keysym.sym),
                    strlen(SDL_GetKeyName(event.key.keysym.sym)))));
                return OBJ_VAL(list);

            default:
                break;
        }
    }

    writeValueArray(&list->elements, OBJ_VAL(copyString("no_event", 8)));
    return OBJ_VAL(list);
}


Value window_getMousePosition(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass, "No method getMousePosition for arity %d.", argCount);
        return NIL_VAL;
    }

    int x, y;
    SDL_GetMouseState(&x, &y);

    ObjList* list = newList();
    writeValueArray(&list->elements, NUMBER_VAL(x));
    writeValueArray(&list->elements, NUMBER_VAL(y));

    return OBJ_VAL(list);
}

Value window_drawCircle(int argCount, Value* args) {
    if (argCount != 4) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method drawCircle for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
        !IS_NUMBER(args[2]) || !IS_NUMBER(args[3])) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "drawCircle(x, y, radius, color) expects arguments (Number, Number, Number, Number).");
        return NIL_VAL;
    }

    int cx = (int)AS_NUMBER(args[0]);
    int cy = (int)AS_NUMBER(args[1]);
    int r  = (int)AS_NUMBER(args[2]);
    int color = (int)AS_NUMBER(args[3]);

    if (!gw.renderer) {
        runtimeError(vm.errorClass, "Renderer failed.");
        return NIL_VAL;
    }

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
    if (!path) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "loadTexture(path) expects argument (String).");
        return NULL;
    }

    SDL_Surface* surface = IMG_Load(path);
    if (!surface) {
        runtimeError(vm.errorClass,
                     "Failed to load texture: %s.", IMG_GetError());
        return NULL;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(gw.renderer, surface);
    SDL_FreeSurface(surface);

    if (!texture) {
        runtimeError(vm.errorClass, "Renderer failed to create texture.");
        return NULL;
    }

    return texture;
}

Value window_loadImage(int argCount, Value* args) {
    if (argCount != 1 && argCount != 3) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "loadImage: no method for %d argument(s).", argCount);
        return NIL_VAL;
    }

    if (argCount == 1) {
        if (!IS_STRING(args[0])) {
            runtimeError(vm.illegalArgumentsErrorClass,
                "loadImage: expected (String) but got (%s).",
                getValueTypeName(args[0]));
            return NIL_VAL;
        }

        ObjString* path = AS_STRING(args[0]);
        SDL_Texture* tex = loadTexture(path->chars);
        if (!tex) return NIL_VAL;

        int texW, texH;
        SDL_QueryTexture(tex, NULL, NULL, &texW, &texH);
        ObjImage* image = newImage(tex, texW, texH);

        return OBJ_VAL(image);
    }

    if (argCount == 3) {
        if (!IS_STRING(args[0]) || !IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
            runtimeError(vm.illegalArgumentsErrorClass,
                "loadImage: expected (String, Number, Number) but got (%s, %s, %s).",
                getValueTypeName(args[0]),
                getValueTypeName(args[1]),
                getValueTypeName(args[2]));
            return NIL_VAL;
        }

        ObjString* path = AS_STRING(args[0]);
        SDL_Texture* tex = loadTexture(path->chars);
        if (!tex) return NIL_VAL;

        int width  = (int)AS_NUMBER(args[1]);
        int height = (int)AS_NUMBER(args[2]);

        ObjImage* image = newImage(tex, width, height);
        return OBJ_VAL(image);
    }

    return NIL_VAL; // unreachable
}

Value window_drawImage(int argCount, Value* args) {
    if (argCount != 3 && argCount != 5) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "drawImage: no method for %d argument(s).", argCount);
        return NIL_VAL;
    }

    if (argCount == 3) {
        if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
            !(IS_OBJ(args[2]) && AS_OBJ(args[2])->type == OBJ_IMAGE)) {

            runtimeError(vm.illegalArgumentsErrorClass,
                "drawImage: expected (Number, Number, Image) but got (%s, %s, %s).",
                getValueTypeName(args[0]),
                getValueTypeName(args[1]),
                getValueTypeName(args[2]));
            return NIL_VAL;
        }

        int x = (int)AS_NUMBER(args[0]);
        int y = (int)AS_NUMBER(args[1]);
        ObjImage* image = (ObjImage*)AS_OBJ(args[2]);

        SDL_Rect dst = {x, y, image->width, image->height};
        SDL_RenderCopy(gw.renderer, image->texture, NULL, &dst);
        return NIL_VAL;
    }

    if (argCount == 5) {
        if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]) ||
            !IS_STRING(args[2]) || !IS_NUMBER(args[3]) || !IS_NUMBER(args[4])) {

            runtimeError(vm.illegalArgumentsErrorClass,
                "drawImage: expected (Number, Number, String, Number, Number) but got (%s, %s, %s, %s, %s).",
                getValueTypeName(args[0]),
                getValueTypeName(args[1]),
                getValueTypeName(args[2]),
                getValueTypeName(args[3]),
                getValueTypeName(args[4]));
            return NIL_VAL;
        }

        int x = (int)AS_NUMBER(args[0]);
        int y = (int)AS_NUMBER(args[1]);
        ObjString* path = AS_STRING(args[2]);
        int w = (int)AS_NUMBER(args[3]);
        int h = (int)AS_NUMBER(args[4]);

        SDL_Texture* tex = loadTexture(path->chars);
        if (!tex) return NIL_VAL;

        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(gw.renderer, tex, NULL, &dst);
        SDL_DestroyTexture(tex);

        return NIL_VAL;
    }

    return NIL_VAL;
}

Value Image_getWidth(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method getWidth for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_IMAGE(args[-1])) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "getWidth: expected receiver (Image) but got (%s).",
                     getValueTypeName(args[-1]));
        return NIL_VAL;
    }

    ObjImage* image = AS_IMAGE(args[-1]);
    return NUMBER_VAL(image->width);
}

Value Image_getHeight(int argCount, Value* args) {
    if (argCount != 0) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "No method getHeight for arity %d.", argCount);
        return NIL_VAL;
    }

    if (!IS_IMAGE(args[-1])) {
        runtimeError(vm.illegalArgumentsErrorClass,
                     "getHeight: expected receiver (Image) but got (%s).",
                     getValueTypeName(args[-1]));
        return NIL_VAL;
    }

    ObjImage* image = AS_IMAGE(args[-1]);
    return NUMBER_VAL(image->height);
}
