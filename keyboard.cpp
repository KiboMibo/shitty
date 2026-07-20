#include "keyboard.h"

bool controlCharacter(int key, bool shifted, uint8_t& character) {
    if (key >= 'A' && key <= 'Z') {
        character = static_cast<uint8_t>(key - 'A' + 1);
        return true;
    }
    switch (key) {
        case ' ':
        case '2':
            character = 0;
            return true;
        case '3':
        case '[':
            character = 27;
            return true;
        case '4':
        case '\\':
            character = 28;
            return true;
        case '5':
        case ']':
            character = 29;
            return true;
        case '6':
            character = 30;
            return true;
        case '7':
            character = 31;
            return true;
        case '8':
            character = 127;
            return true;
        case '-':
            character = shifted ? 31 : static_cast<uint8_t>(key);
            return true;
        case '/':
            character = shifted ? 127 : 31;
            return true;
        default:
            if (key > 0 && key < 128) {
                character = static_cast<uint8_t>(key);
                return true;
            }
            return false;
    }
}
