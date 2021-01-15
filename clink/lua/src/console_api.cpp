// Copyright (c) 2021 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "pch.h"
#include "lua_state.h"
#include "terminal/scroll.h"
#include "terminal/printer.h"

#include <core/base.h>
#include <core/str.h>

//------------------------------------------------------------------------------
extern "C" int _rl_vis_botlin;
extern "C" int _rl_last_v_pos;

//------------------------------------------------------------------------------
extern printer* g_printer;

//------------------------------------------------------------------------------
static SHORT GetConsoleNumLines(const CONSOLE_SCREEN_BUFFER_INFO& csbi)
{
    // Calculate the bottom as the line immediately preceding the beginning of
    // the Readline input line.  That may contain some of the prompt text, if
    // the prompt text is more than one line.
    SHORT bottom_Y = csbi.dwCursorPosition.Y - _rl_last_v_pos;
    return bottom_Y + 1;
}



//------------------------------------------------------------------------------
/// -name:  console.scroll
/// -arg:   mode:string
/// -arg:   amount:integer
/// -ret:   integer
/// Scrolls the console screen buffer and returns the number of lines scrolled
/// up (negative) or down (positive).
///
/// The <span class="arg">mode</span> specifies how to scroll:
/// <table>
/// <tr><th>Mode</th><th>Description</th></tr>
/// <tr><td>"line"</td><td>Scrolls by <span class="arg">amount</span> lines;
/// negative is up and positive is down.
/// <tr><td>"page"</td><td>Scrolls by <span class="arg">amount</span> pages;
/// negative is up and positive is down.
/// <tr><td>"end"</td><td>Scrolls to the top if <span class="arg">amount</span>
/// is negative, or to the bottom if positive.
/// <tr><td>"absolute"</td><td>Scrolls to line <span class="arg">amount</span>,
/// from 1 to <a href="#console.getnumlines">console.getnumlines()</a>.
/// </table>
static int scroll(lua_State* state)
{
    if (!lua_isstring(state, 1)) return 0;
    if (!lua_isnumber(state, 2)) return 0;

    const char* mode = lua_tostring(state, 1);
    int amount = int(lua_tointeger(state, 2));

    SCRMODE scrmode = SCR_BYLINE;
    if (stricmp(mode, "page") == 0)
        scrmode = SCR_BYPAGE;
    else if (stricmp(mode, "end") == 0)
        scrmode = SCR_TOEND;
    else if (stricmp(mode, "absolute") == 0)
        scrmode = SCR_ABSOLUTE;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    int scrolled = ScrollConsoleRelative(h, amount, scrmode);
    lua_pushinteger(state, scrolled);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getwidth
/// -ret:   integer
/// Returns the width of the console screen buffer in characters.
static int get_width(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.dwSize.X);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getheight
/// -ret:   integer
/// Returns the number of visible lines of the console screen buffer.
static int get_height(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.srWindow.Bottom + 1 - csbiInfo.srWindow.Top);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getnumlines
/// -ret:   integer
/// Returns the total number of lines in the console screen buffer.
static int get_num_lines(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, GetConsoleNumLines(csbiInfo));
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.gettop
/// -ret:   integer
/// Returns the current top line (scroll position) in the console screen buffer.
static int get_top(lua_State* state)
{
    CONSOLE_SCREEN_BUFFER_INFO csbiInfo;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbiInfo))
        return 0;

    lua_pushinteger(state, csbiInfo.srWindow.Top + 1);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.getlinetext
/// -arg:   line:integer
/// -ret:   string
/// Returns the text from line number <span class="arg">line</span>, from 1 to
/// <a href="#console.getnumlines">console.getnumlines</a>.
static int get_line_text(lua_State* state)
{
    if (!g_printer)
        return 0;

    if (!lua_isnumber(state, 1))
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT line = int(lua_tointeger(state, 1)) - 1;
    line = min<int>(line, GetConsoleNumLines(csbi));
    line = max<int>(line, 0);

    str_moveable out;
    if (!g_printer->get_line_text(line, out))
        return 0;

    lua_pushlstring(state, out.c_str(), out.length());
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.islinedefaultcolor
/// -arg:   line:integer
/// -ret:   boolean
/// Returns whether line number <span class="arg">line</span> uses only the
/// default text color.
static int is_line_default_color(lua_State* state)
{
    if (!g_printer)
        return 0;

    if (!lua_isnumber(state, 1))
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT line = int(lua_tointeger(state, 1)) - 1;
    line = min<int>(line, GetConsoleNumLines(csbi));
    line = max<int>(line, 0);

    int result = g_printer->is_line_default_color(line);

    if (result < 0)
        return 0;

    lua_pushboolean(state, result > 0);
    return 1;
}

//------------------------------------------------------------------------------
/// -name:  console.linehascolor
/// -arg:   line:integer
/// -arg:   [attr:integer]
/// -arg:   [attrs:table of integers]
/// -arg:   [mask:string]
/// -ret:   boolean
/// Returns whether line number <span class="arg">line</span> contains the DOS
/// color code <span class="arg">attr</span>, or any of the DOS color codes in
/// <span class="arg">attrs</span> (either an integer or a table of integers
/// must be provided, but not both).  <span class="arg">mask</span> is optional
/// and can be "fore" or "back" to only match foreground or background colors,
/// respectively.
///
/// The low 4 bits of the color code are the foreground color, and the high 4
/// bits of the color code are the background color.  This refers to the default
/// 16 color palette used by console windows.  When 256 color or 24-bit color
/// ANSI escape codes have been used, the closest of the 16 colors is used.
///
/// To build a color code, add the corresponding Foreground color and the
/// Background color values from this table:
///
/// <table><tr><th align="center">Foreground</th><th align="center">Background</th><th>Color</th></tr>
/// <tr><td align="center">0</td><td align="center">0</td><td><div class="colorsample" style="background-color:#000000">&nbsp;</div> Black</td></tr>
/// <tr><td align="center">1</td><td align="center">16</td><td><div class="colorsample" style="background-color:#000080">&nbsp;</div> Dark Blue</td></tr>
/// <tr><td align="center">2</td><td align="center">32</td><td><div class="colorsample" style="background-color:#008000">&nbsp;</div> Dark Green</td></tr>
/// <tr><td align="center">3</td><td align="center">48</td><td><div class="colorsample" style="background-color:#008080">&nbsp;</div> Dark Cyan</td></tr>
/// <tr><td align="center">4</td><td align="center">64</td><td><div class="colorsample" style="background-color:#800000">&nbsp;</div> Dark Red</td></tr>
/// <tr><td align="center">5</td><td align="center">80</td><td><div class="colorsample" style="background-color:#800080">&nbsp;</div> Dark Magenta</td></tr>
/// <tr><td align="center">6</td><td align="center">96</td><td><div class="colorsample" style="background-color:#808000">&nbsp;</div> Dark Yellow</td></tr>
/// <tr><td align="center">7</td><td align="center">112</td><td><div class="colorsample" style="background-color:#c0c0c0">&nbsp;</div> Gray</td></tr>
/// <tr><td align="center">8</td><td align="center">128</td><td><div class="colorsample" style="background-color:#808080">&nbsp;</div> Dark Gray</td></tr>
/// <tr><td align="center">9</td><td align="center">144</td><td><div class="colorsample" style="background-color:#0000ff">&nbsp;</div> Bright Blue</td></tr>
/// <tr><td align="center">10</td><td align="center">160</td><td><div class="colorsample" style="background-color:#00ff00">&nbsp;</div> Bright Green</td></tr>
/// <tr><td align="center">11</td><td align="center">176</td><td><div class="colorsample" style="background-color:#00ffff">&nbsp;</div> Bright Cyan</td></tr>
/// <tr><td align="center">12</td><td align="center">192</td><td><div class="colorsample" style="background-color:#ff0000">&nbsp;</div> Bright Red</td></tr>
/// <tr><td align="center">13</td><td align="center">208</td><td><div class="colorsample" style="background-color:#ff00ff">&nbsp;</div> Bright Magenta</td></tr>
/// <tr><td align="center">14</td><td align="center">224</td><td><div class="colorsample" style="background-color:#ffff00">&nbsp;</div> Bright Yellow</td></tr>
/// <tr><td align="center">15</td><td align="center">240</td><td><div class="colorsample" style="background-color:#ffffff">&nbsp;</div> White</td></tr>
/// </table>
static int line_has_color(lua_State* state)
{
    if (!g_printer)
        return 0;

    if (!lua_isnumber(state, 1))
        return 0;
    if (!lua_isnumber(state, 2) && !lua_istable(state, 2))
        return 0;
    if (!lua_isnil(state, 3) && !lua_isstring(state, 3))
        return 0;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(h, &csbi))
        return 0;

    SHORT line = int(lua_tointeger(state, 1)) - 1;
    line = min<int>(line, GetConsoleNumLines(csbi));
    line = max<int>(line, 0);

    BYTE mask = 0xff;
    {
        const char* mask_name = lua_tostring(state, 3);
        if (mask_name)
        {
            if (strcmp(mask_name, "fore") == 0)         mask = 0x0f;
            else if (strcmp(mask_name, "back") == 0)    mask = 0xf0;
            else if (strcmp(mask_name, "both") == 0)    mask = 0xff;
        }
    }

    int result;

    if (lua_isnumber(state, 2))
    {
        BYTE attr = BYTE(lua_tointeger(state, 2));
        result = g_printer->line_has_color(line, &attr, 1, mask);
    }
    else
    {
        BYTE attrs[32];
        int num_attrs = 0;

        for (int i = 1; num_attrs <= sizeof_array(attrs); i++)
        {
            lua_rawgeti(state, 2, i);
            if (lua_isnil(state, -1))
            {
                lua_pop(state, 1);
                break;
            }

            attrs[num_attrs++] = BYTE(lua_tointeger(state, -1));

            lua_pop(state, 1);
        }
        result = g_printer->line_has_color(line, attrs, num_attrs, mask);
    }

    if (result < 0)
        return 0;

    lua_pushboolean(state, result > 0);
    return 1;
}

//------------------------------------------------------------------------------
void console_lua_initialise(lua_state& lua)
{
    struct {
        const char* name;
        int         (*method)(lua_State*);
    } methods[] = {
        { "scroll",                 &scroll },
        { "getwidth",               &get_width },
        { "getheight",              &get_height },
        { "getnumlines",            &get_num_lines },
        { "gettop",                 &get_top },
        { "getlinetext",            &get_line_text },
        { "islinedefaultcolor",     &is_line_default_color },
        { "linehascolor",           &line_has_color },
//        { "findline",               &find_line },
    };

    lua_State* state = lua.get_state();

    lua_createtable(state, sizeof_array(methods), 0);

    for (const auto& method : methods)
    {
        lua_pushstring(state, method.name);
        lua_pushcfunction(state, method.method);
        lua_rawset(state, -3);
    }

    lua_setglobal(state, "console");
}