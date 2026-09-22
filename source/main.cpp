#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_W 400
#define SCREEN_H 240

typedef struct {
    float x, y, w, h;
    const char *label;
} Button;

static bool pointInButton(const Button *b, float px, float py) {
    return px >= b->x && px <= b->x + b->w &&
           py >= b->y && py <= b->y + b->h;
}

// 调出系统软键盘，返回用户是否确认
static bool showKeyboard(char *out, size_t outSize, const char *hint) {
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)outSize - 1);
    swkbdSetHintText(&swkbd, hint);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);

    SwkbdButton pressed = swkbdInputText(&swkbd, out, outSize);
    return pressed == SWKBD_BUTTON_RIGHT;
}

int main(int argc, char **argv) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    consoleInit(GFX_TOP, NULL);

    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    Button btn = {
        .x = (SCREEN_W - 200) / 2.0f,
        .y = (SCREEN_H - 48)  / 2.0f,
        .w = 200,
        .h = 48,
        .label = "bv-download"
    };

    bool pressed = false;

    // 保存输入内容的变量
    char bvin[64] = {0};

    printf("bv-download demo\n");
    printf("Touch the button or press A.\n");
    printf("START to exit.\n");

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START) break;

        touchPosition touch;
        hidTouchRead(&touch);

        bool touched = (kDown & KEY_TOUCH) &&
                       pointInButton(&btn, touch.px, touch.py);
        bool keyPressed = (kDown & KEY_A) != 0;

        if (touched || keyPressed) {
            pressed = true;

            // 调出系统输入框
            char buf[64] = {0};
            if (showKeyboard(buf, sizeof(buf), "Enter BV number")) {
                strncpy(bvin, buf, sizeof(bvin) - 1);
                bvin[sizeof(bvin) - 1] = '\0';
                printf("bvin = %s\n", bvin);
            } else {
                printf("Input cancelled.\n");
            }

            // 等按键/触摸释放，避免连续触发
            while (aptMainLoop()) {
                hidScanInput();
                if (!(hidKeysHeld() & (KEY_TOUCH | KEY_A))) break;
                gspWaitForVBlank();
            }
        } else {
            pressed = false;
        }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(bottom, C2D_Color32(30, 30, 40, 255));
        C2D_SceneBegin(bottom);

        u32 btnColor = pressed
            ? C2D_Color32(60, 140, 220, 255)
            : C2D_Color32(80, 80, 100, 255);
        C2D_DrawRectSolid(btn.x, btn.y, 0.0f, btn.w, btn.h, btnColor);

        // 边框
        u32 border = C2D_Color32(200, 200, 220, 255);
        C2D_DrawRectSolid(btn.x, btn.y, 0.0f, btn.w, 2.0f, border);
        C2D_DrawRectSolid(btn.x, btn.y + btn.h - 2.0f, 0.0f, btn.w, 2.0f, border);
        C2D_DrawRectSolid(btn.x, btn.y, 0.0f, 2.0f, btn.h, border);
        C2D_DrawRectSolid(btn.x + btn.w - 2.0f, btn.y, 0.0f, 2.0f, btn.h, border);

        C3D_FrameEnd(0);
    }

    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
