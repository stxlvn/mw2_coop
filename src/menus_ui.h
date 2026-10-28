#pragma once
// Menu structures of this 64-bit build and helpers shared by menus.c and access.c (see menus.c).
#include <stdint.h>

#ifdef _WIN64
#define ITEM_SIZE 0x1e8
#define MENU_SIZE 0x200 // menuDef_t is smaller; copies only need the fields up to items (+0x168)
#define MENU_ITEM_COUNT 0xbc
#define MENU_ON_OPEN 0xe0
#define MENU_ON_CLOSE 0xf0
#define MENU_ITEMS 0x168
#define ITEM_RECT 0x08 // rectDef_t {x, y, w, h floats, horzAlign, vertAlign chars}
#define ITEM_RECT_Y 0x0c
#define ITEM_RECT_ALIGN_H 0x18 // char: 1 left (the lobby's buttons), 3 right
#define ITEM_RECT_ALIGN_V 0x19 // char: 1 top, 3 bottom
#define ITEM_RECT_CLIENT_Y 0x20
#define ITEM_DYNAMIC_FLAGS 0x50
#define ITEM_TYPE 0xc4 // int: 1 button, 4 edit field, 16 file-name field
#define ITEM_DATA_TYPE 0xc8
#define ITEM_TYPE_EDITFIELD 4
#define ITEM_TEXT 0xf0
#define ITEM_PARENT 0x100
#define ITEM_ACTION 0x128
#define ITEM_ACCEPT 0x130
#define ITEM_ON_FOCUS 0x138
#define ITEM_LEAVE_FOCUS 0x140
#define ITEM_DVAR 0x148
#define ITEM_TYPE_DATA 0x188 // itemDefData_t: editFieldDef * for edit fields
#define ITEM_TEXT_EXP 0x1b0
#define ITEM_FLOAT_EXPS 0x198 // floatExpressions (visibleExp is +0x1a0, disabledExp +0x1a8)
#define ITEM_DISABLED_EXP 0x1a8
#define ITEM_BACKGROUND_PTR 0xa8 // window background Material *
#define ITEM_MATERIAL_EXP_PTR 0x1b8
#else
// 32-bit: the layouts of iw4x-sp's structs.hpp (itemDef_s 0x17c bytes, menuDef_t 0x190, windowDef_t 0xa4, rectDef_s 0x14,
// 4-byte pointers), added up by hand; static_asserts there give the sizes
#define ITEM_SIZE 0x17c
#define MENU_SIZE 0x1a0 // menuDef_t is 0x190; copies read a little over, as in 64 bits
#define MENU_ITEM_COUNT 0xac
#define MENU_ON_OPEN 0xcc
#define MENU_ON_CLOSE 0xd4
#define MENU_ITEMS 0x118
#define ITEM_RECT 0x04
#define ITEM_RECT_Y 0x08
#define ITEM_RECT_ALIGN_H 0x14
#define ITEM_RECT_ALIGN_V 0x15
#define ITEM_RECT_CLIENT_Y 0x1c
#define ITEM_DYNAMIC_FLAGS 0x48
#define ITEM_TYPE 0xb8
#define ITEM_DATA_TYPE 0xbc
#define ITEM_TYPE_EDITFIELD 4
#define ITEM_TEXT 0xe4
#define ITEM_PARENT 0xec
#define ITEM_ACTION 0x100
#define ITEM_ACCEPT 0x104
#define ITEM_ON_FOCUS 0x108
#define ITEM_LEAVE_FOCUS 0x10c
#define ITEM_DVAR 0x110
#define ITEM_TYPE_DATA 0x134
#define ITEM_TEXT_EXP 0x14c
#define ITEM_FLOAT_EXPS 0x140
#define ITEM_DISABLED_EXP 0x148
#define ITEM_BACKGROUND_PTR 0xa0
#define ITEM_MATERIAL_EXP_PTR 0x150
#endif
#define WINDOW_HASFOCUS 2
#define WINDOW_VISIBLE 4
#define BUTTON_HEIGHT 20.0f

// The menus the UI has loaded; NULL if not loaded. Main thread.
uint8_t *ui_find_menu(const char *name);
// Index of the item named <name> in <menu>, or -1.
int ui_item_index(uint8_t *menu, const char *name);
// A handler set running one menu script (the zone's parsed form: quoted tokens, ';' ends a command).
void *ui_script_set(const char *script);
// A copy of menu <from> named <name>, every item copied too; not yet in the UI's list.
uint8_t *ui_clone_menu(uint8_t *from, const char *name);
// Add a menu to the UI's list (Menus_FindByName and "openmenu" find it then); 0 if the list is full.
int ui_register_menu(uint8_t *menu);
// A copy of an item for another menu (or the same one), without focus.
uint8_t *ui_clone_item(uint8_t *from, uint8_t *parent_menu);
// A copy of a handler set with <from> replaced by <to> in its script strings; NULL if no script had <from>.
void *ui_script_replace(const void *set, const char *from, const char *to);
