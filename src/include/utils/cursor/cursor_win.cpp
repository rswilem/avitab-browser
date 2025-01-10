#if IBM
#include "cursor.h"
#include <windows.h>
void initializeCursor() {}

void destroyCursor() {}

void setCursor(CursorType cursor) {
    switch (cursor) {
        case CursorHand:
            SetCursor(LoadCursor(NULL, IDC_HAND));
            break;
            
        case CursorText:
            SetCursor(LoadCursor(NULL, IDC_IBEAM));
            break;
            
        default:
            break;
    }
}
#endif
