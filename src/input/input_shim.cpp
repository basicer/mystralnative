/**
 * Input Event Shim - SDL3 to Web Events
 *
 * Translates SDL3 native events into Web-style events that game code expects.
 * This allows browser-targeted game code to work unchanged.
 *
 * SDL3 Event -> Web Event mapping:
 * - SDL_EVENT_KEY_DOWN/UP -> KeyboardEvent (keydown/keyup)
 * - SDL_EVENT_MOUSE_MOTION -> MouseEvent (mousemove)
 * - SDL_EVENT_MOUSE_BUTTON_* -> MouseEvent (mousedown/mouseup/click)
 * - SDL_EVENT_MOUSE_WHEEL -> WheelEvent
 * - SDL_EVENT_FINGER_* -> TouchEvent
 * - SDL_EVENT_GAMEPAD_* -> Gamepad API state updates
 * - SDL_EVENT_WINDOW_RESIZED -> resize event
 */

#include <iostream>
#include <map>
#include <string>

namespace mystral {
namespace input {

/**
 * Gamepad state (for polling-based Web Gamepad API)
 */
struct GamepadState {
    bool connected = false;
    std::string id;
    double axes[4] = {0};      // Standard mapping: left stick X/Y, right stick X/Y
    bool buttons[17] = {0};    // Standard mapping: A, B, X, Y, LB, RB, LT, RT, ...
    double buttonValues[17] = {0};  // Analog button values
};

static GamepadState gamepads[4];

/**
 * Initialize input shim
 */
bool initInputShim(void* jsContext) {
    std::cout << "[Input] Initializing input shim..." << std::endl;

    // TODO: Set up global event listeners in JS
    // TODO: Set up navigator.getGamepads()

    return true;
}

/**
 * Process SDL event and dispatch to JS
 */
void processSDLEvent(void* sdlEvent, void* jsContext) {
    // TODO: Cast sdlEvent to SDL_Event*
    // TODO: Switch on event type
    // TODO: Create Web-style event object
    // TODO: Dispatch to JS

    // Example structure (pseudocode):
    /*
    SDL_Event* event = (SDL_Event*)sdlEvent;
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            JSObject keyEvent;
            keyEvent.set("type", event->type == SDL_EVENT_KEY_DOWN ? "keydown" : "keyup");
            keyEvent.set("key", sdlKeyToWebKey(event->key.key));
            keyEvent.set("code", sdlScancodeToWebCode(event->key.scancode));
            keyEvent.set("altKey", (event->key.mod & SDL_KMOD_ALT) != 0);
            keyEvent.set("ctrlKey", (event->key.mod & SDL_KMOD_CTRL) != 0);
            keyEvent.set("shiftKey", (event->key.mod & SDL_KMOD_SHIFT) != 0);
            keyEvent.set("metaKey", (event->key.mod & SDL_KMOD_GUI) != 0);
            keyEvent.set("repeat", event->key.repeat != 0);
            dispatchEvent(jsContext, keyEvent);
            break;
        }
        // ... etc for mouse, gamepad, touch
    }
    */
}

/**
 * Get gamepads array for navigator.getGamepads()
 */
void* getGamepads(void* jsContext) {
    // TODO: Create JS array of gamepad objects
    // Return null for disconnected slots

    return nullptr;
}

/**
 * SDL keycode to Web key string
 * See: https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/key/Key_Values
 */
const char* sdlKeyToWebKey(int sdlKeycode) {
    // TODO: Full mapping table
    // This is a subset for illustration
    switch (sdlKeycode) {
        // Letters (SDL uses lowercase)
        case 'a': return "a";
        case 'b': return "b";
        case 'c': return "c";
        case 'd': return "d";
        case 'e': return "e";
        case 'f': return "f";
        case 'g': return "g";
        case 'h': return "h";
        case 'i': return "i";
        case 'j': return "j";
        case 'k': return "k";
        case 'l': return "l";
        case 'm': return "m";
        case 'n': return "n";
        case 'o': return "o";
        case 'p': return "p";
        case 'q': return "q";
        case 'r': return "r";
        case 's': return "s";
        case 't': return "t";
        case 'u': return "u";
        case 'v': return "v";
        case 'w': return "w";
        case 'x': return "x";
        case 'y': return "y";
        case 'z': return "z";
        case '0': return "0";
        case '1': return "1";
        case '2': return "2";
        case '3': return "3";
        case '4': return "4";
        case '5': return "5";
        case '6': return "6";
        case '7': return "7";
        case '8': return "8";
        case '9': return "9";
        case '-': return "-";
        case '=': return "=";
        case '[': return "[";
        case ']': return "]";
        case ';': return ";";
        case '\'': return "'";
        case ',': return ",";
        case '.': return ".";
        case '/': return "/";
        case '\\': return "\\";
        case '`': return "`";

        // Special keys
        case '\r': return "Enter";
        case '\t': return "Tab";
        case ' ': return " ";
        case 27:  return "Escape";  // SDLK_ESCAPE

        // Arrow keys (SDL uses special constants)
        // case SDLK_UP: return "ArrowUp";
        // case SDLK_DOWN: return "ArrowDown";
        // case SDLK_LEFT: return "ArrowLeft";
        // case SDLK_RIGHT: return "ArrowRight";

        default: return "";
    }
}

/**
 * SDL scancode to Web code string
 * See: https://developer.mozilla.org/en-US/docs/Web/API/KeyboardEvent/code
 */
const char* sdlScancodeToWebCode(int scancode) {
    // TODO: Full mapping table
    // Web codes are like "KeyA", "Enter", "ArrowUp", etc.
    return "";
}

/**
 * SDL mouse button to Web button index
 * Web: 0=left, 1=middle, 2=right, 3=back, 4=forward
 * SDL: 1=left, 2=middle, 3=right, 4=X1, 5=X2
 */
int sdlButtonToWebButton(int sdlButton) {
    switch (sdlButton) {
        case 1: return 0;  // Left
        case 2: return 1;  // Middle
        case 3: return 2;  // Right
        case 4: return 3;  // Back
        case 5: return 4;  // Forward
        default: return 0;
    }
}

}  // namespace input
}  // namespace mystral
