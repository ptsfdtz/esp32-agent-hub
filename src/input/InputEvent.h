#pragma once
#include <stdint.h>
enum class InputEvent : uint8_t {
    NONE, ROTATE_LEFT, ROTATE_RIGHT, PUSH, PUSH_LONG,
    BACK, BACK_LONG, CONFIRM, CONFIRM_LONG
};
