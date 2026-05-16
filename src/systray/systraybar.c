/**************************************************************************
* Tint2 : systraybar
*
* Copyright (C) 2009 thierry lorthiois (lorthiois@bbsoft.fr) from Omega distribution
* based on 'docker-1.5' from Ben Jansens.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License version 2
* as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**************************************************************************/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <glib.h>
#include <Imlib2.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <unistd.h>

#include "systraybar.h"
#include "server.h"
#include "panel.h"
#include "window.h"

GSList *icons;

/* defined in the systray spec */
#define SYSTEM_TRAY_REQUEST_DOCK 0
#define SYSTEM_TRAY_BEGIN_MESSAGE 1
#define SYSTEM_TRAY_CANCEL_MESSAGE 2

// selection window
Window net_sel_win = None;

// freedesktop specification doesn't allow multi systray
Systray systray;
bool refresh_systray;
bool systray_enabled;
int systray_max_icon_size;
int systray_monitor;
int chrono;
int systray_composited;
bool systray_profile;
char *systray_hide_name_filter;
regex_t *systray_hide_name_regex;
// background pixmap if we render ourselves the icons
static Pixmap render_background;

const int min_refresh_period = 50;
const int max_fast_refreshes = 5;
const int resize_period_threshold = 1000;
const int fast_resize_period = 50;
const int slow_resize_period = 5000;
const int min_bad_resize_events = 3;
const int max_bad_resize_events = 10;

int systray_get_desired_size(void *obj);
void systray_dump_geometry(void *obj, int indent);

void default_systray()
{
    systray_enabled = false;
    memset(&systray, 0, sizeof(systray));
    render_background = None;
    chrono = 0;
    systray.alpha = 100;
    systray.sort = SYSTRAY_SORT_LEFT2RIGHT;
    systray.area._draw_foreground = draw_systray;
    systray.area._on_change_layout = on_change_systray;
    systray.area.size_mode = LAYOUT_FIXED;
    systray.area._resize = resize_systray;
    systray_profile = getenv("SYSTRAY_PROFILING") != NULL;
    systray_hide_name_filter = NULL;
    systray_hide_name_regex = NULL;
}

void cleanup_systray()
{
    stop_net();
    systray_enabled = false;
    systray_max_icon_size = 0;
    systray_monitor = 0;
    systray.area.on_screen = false;
    free_area(&systray.area);
    if (render_background) {
        XFreePixmap(server.display, render_background);
        render_background = None;
    }
    if (systray_hide_name_regex) {
        regfree(systray_hide_name_regex);
        free_and_null(systray_hide_name_regex);
    }
    if (systray_hide_name_filter)
        free_and_null(systray_hide_name_filter);
}

void init_systray()
{
    if (!systray_enabled)
        return;

    systray_composited = !server.disable_transparency && server.visual32 && server.colormap32;
    fprintf(stderr, "tint2: Systray composited rendering %s\n", systray_composited ? "on" : "off");

    if (!systray_composited) {
        fprintf(stderr, "tint2: systray_asb forced to 100 0 0\n");
        systray.alpha = 100;
        systray.brightness = systray.saturation = 0;
    }
}

void init_systray_panel(void *p)
{
    Panel *panel = p;
    systray.area.parent = panel;
    systray.area.panel = panel;
    systray.area._dump_geometry = systray_dump_geometry;
    systray.area._get_desired_size = systray_get_desired_size;
    snprintf (systray.area.name, strlen_const(systray.area.name), "Systray");
    if (!systray.area.bg)
        systray.area.bg = &g_array_index(backgrounds, Background, 0);
    show(&systray.area);
    schedule_redraw(&systray.area);
    refresh_systray = true;
    area_gradients_create(&systray.area);
}

void systray_get_geometry(int *size)
{
    Panel *panel = systray.area.panel;
    double scale = panel->scale;
    int col_icons, row_icons,
        spacing   = systray.area.spacing,
        icon_size = systray.icon_size
                    = (panel_horizontal ? systray.area.height : systray.area.width)
                    - MAX( left_right_border_width( & systray.area), top_bottom_border_width( & systray.area))
                    - 2 * systray.area.paddingy * scale;
    if (systray_max_icon_size)
        icon_size = MIN( icon_size, systray_max_icon_size * scale);

    int count = 0;
    for (GSList *l = systray.list_icons; l; l = l->next)
        count++;

    if (panel_horizontal) {
        int height = systray.area.height - top_bottom_border_width( & systray.area) - 2 * systray.area.paddingy * scale;
        // here col_icons always higher than 0
        col_icons = (spacing * scale + height) /
                    (spacing * scale + icon_size);
        systray.margin  = height - icon_size
                        - (col_icons - 1) * (icon_size + spacing * scale);
        row_icons   = (count / col_icons)
                    + (count % col_icons != 0);
        *size = left_right_border_width( & systray.area)
                + 2 * systray.area.paddingx * scale
                + (row_icons      * icon_size)
                + (row_icons - 1) * spacing * scale;
    } else {
        int width = systray.area.width - left_right_border_width( & systray.area) - 2 * systray.area.paddingy * scale;
        // here row_icons always higher than 0
        row_icons = (spacing * scale + width) /
                    (spacing * scale + icon_size);
        systray.margin  = width - icon_size
                        - (row_icons - 1) * (icon_size + spacing * scale);
        col_icons   = (count / row_icons)
                    + (count % row_icons != 0);
        *size = top_bottom_border_width( & systray.area)
                + (2 * systray.area.paddingx * scale)
                + (col_icons      * icon_size)
                + (col_icons - 1) * spacing * scale;
    }
    systray.icons_per_column = col_icons;
    systray.icons_per_row    = row_icons;
}

int systray_get_desired_size(void *obj)
{
    int size;
    systray_get_geometry(&size);
    return size;
}

bool resize_systray(void *obj)
{
    if (systray_profile)
    {
        fprintf( stderr, "tint2: [%f] %s:%d\n", profiling_get_time(), __func__, __LINE__);
    }

    int size;
    systray_get_geometry(&size);

    bool result = refresh_systray;

    if (net_sel_win == None)
    {
        start_net();
        result = true;
    }
    else if (systray.icon_size > 0)
    {
        long icon_size = systray.icon_size;
        XChangeProperty(server.display, net_sel_win,
                        server.atom [_NET_SYSTEM_TRAY_ICON_SIZE],
                        XA_CARDINAL,
                        32,
                        PropModeReplace,
                        (unsigned char *)&icon_size,
                        1);
    }

    if (panel_horizontal)
    {
        if (systray.area.width != size)
        {
            systray.area.width = size;
            result = true;
        }
    }
    else
    {
        if (systray.area.height != size)
        {
            systray.area.height = size;
            result = true;
        }
    }

    on_change_systray(&systray.area);

    return result;
}

void draw_systray(void *obj, cairo_t *c)
{
    if (systray_profile)
        fprintf(stderr, BLUE "tint2: [%f] %s:%d" RESET "\n", profiling_get_time(), __func__, __LINE__);
    if (systray_composited) {
        if (render_background)
            XFreePixmap(server.display, render_background);
        render_background = XCreatePixmap(  server.display,     server.root_win,
                                            systray.area.width, systray.area.height, server.depth);
        XCopyArea(server.display,
                  systray.area.pix, render_background, server.gc,
                  0, 0,
                  systray.area.width, systray.area.height,
                  0, 0);
    }

    refresh_systray = true;
}

void systray_dump_geometry(void *obj, int indent)
{
    Systray *tray = obj;

    fprintf(stderr, "tint2: %*sIcons:\n", indent, "");
    indent += 2;
    for (GSList *l = tray->list_icons; l; l = l->next)
    {
        TrayWindow *pTrayWin = l->data;
        fprintf(stderr,
                "tint2: %*sIcon: x = %d, y = %d, w = %d, h = %d, name = %s\n",
                indent, "",
                pTrayWin->x, pTrayWin->y,
                pTrayWin->width, pTrayWin->height,
                pTrayWin->name);
    }
}

void on_change_systray(void *obj)
{
    if (systray_profile)
    {
        fprintf(stderr, "tint2: [%f] %s:%d\n", profiling_get_time(), __func__, __LINE__);
    }

    if (systray.icons_per_column == 0 || systray.icons_per_row == 0)
    {
        return;
    }


    // systray.area.posx/posy are computed by rendering engine.
    // Based on this we calculate the positions of the tray icons.
    Panel *panel = systray.area.panel;
    int posx, posy;
    int start;
    if (panel_horizontal)
    {
        posy = start    = top_border_width( & panel->area)  + panel->area.paddingy  * panel->scale
                        + top_border_width( & systray.area) + systray.area.paddingy * panel->scale + systray.margin / 2;
        posx = systray.area.posx + left_border_width( & systray.area) + systray.area.paddingx * panel->scale;
    }
    else
    {
        posx = start    = left_border_width( & panel->area)  + panel->area.paddingy  * panel->scale
                        + left_border_width( & systray.area) + systray.area.paddingy * panel->scale + systray.margin / 2;
        posy = systray.area.posy + top_border_width( & systray.area) + systray.area.paddingx * panel->scale;
    }

    bool reorganized;
    do
    {
        reorganized = false;
        TrayWindow* pTrayWin;
        GSList* pList = systray.list_icons;
        
        for (int i = 1; pList != NULL; pList = pList->next, i++)
        {
            pTrayWin = pList->data;
            pTrayWin->y = posy;
            pTrayWin->x = posx;
            pTrayWin->width =
            pTrayWin->height = systray.icon_size;
            if (systray_profile)
            {
                fprintf(stderr,
                        "%s:%d win = %lu (%s), parent = %lu, x = %d, y = %d\n",
                        __func__, __LINE__,
                        pTrayWin->win, pTrayWin->name, pTrayWin->parent,
                        posx, posy);
            }

            int pos = systray.icon_size + systray.area.spacing * panel->scale;
            if (panel_horizontal)
            {
                if (i % systray.icons_per_column)
                {
                    posy += pos;
                }
                else
                {
                    posy = start, posx += pos;
                }
            }
            else
            {
                if (i % systray.icons_per_row)
                {
                    posx += pos;
                }
                else
                {
                    posx = start, posy += pos;
                }
            }

            // position and size the icon window
            unsigned int border_width;
            int xpos, ypos;
            unsigned int width, height, depth;
            Window root;
            if (!XGetGeometry(server.display,
                            pTrayWin->parent,
                            &root,
                            &xpos,
                            &ypos,
                            &width,
                            &height,
                            &border_width,
                            &depth))
            {
                fprintf(stderr, RED "tint2: Couldn't get geometry of window!" RESET "\n");
            }

            bool move = (xpos != pTrayWin->x) || (ypos != pTrayWin->y);
            bool resize = (width != pTrayWin->width) || (height != pTrayWin->height);

            if (move && resize)
            {
                if (systray_profile)
                {
                    fprintf(stderr,
                            "XMoveResizeWindow(server.display, pTrayWin->parent = %ld, pTrayWin->x = %d, pTrayWin->y = %d, "
                            "pTrayWin->width = %d, pTrayWin->height = %d)\n",
                            pTrayWin->parent,
                            pTrayWin->x,     pTrayWin->y,
                            pTrayWin->width, pTrayWin->height);
                }

                XMoveResizeWindow(server.display,
                                  pTrayWin->parent,
                                  pTrayWin->x,
                                  pTrayWin->y,
                                  pTrayWin->width,
                                  pTrayWin->height);
            }
            else if (move)
            {
                if (systray_profile)
                {
                    fprintf(stderr,
                            "XMoveWindow(server.display, pTrayWin->parent = %ld, pTrayWin->x = %d, pTrayWin->y = %d)\n",
                            pTrayWin->parent,
                            pTrayWin->x, pTrayWin->y);
                }
                XMoveWindow(server.display, pTrayWin->parent, pTrayWin->x, pTrayWin->y);
            }
            else if (resize)
            {
                if (systray_profile)
                {
                    fprintf(stderr,
                            "XResizeWindow(server.display, pTrayWin->parent = %ld, "
                            "pTrayWin->width = %d, pTrayWin->height = %d)\n",
                            pTrayWin->parent,
                            pTrayWin->width, pTrayWin->height);
                }

                XResizeWindow(server.display, pTrayWin->parent, pTrayWin->width, pTrayWin->height);
            }

            if (!pTrayWin->reparented)
            {
                reorganized = !reparent_icon(pTrayWin);
                break;
            }
        }
    }
    while (reorganized);

    refresh_systray = true;
}

// ***********************************************
// systray protocol

void start_net()
{
    if (systray_profile)
        fprintf(stderr, "tint2: [%f] %s:%d\n", profiling_get_time(), __func__, __LINE__);
    if (net_sel_win) {
        // protocol already started
        if (!systray_enabled)
            stop_net();
        return;
    } else {
        if (!systray_enabled)
            return;
    }

    // freedesktop systray specification
    Window win = XGetSelectionOwner(server.display, server.atom [_NET_SYSTEM_TRAY_SCREEN]);
    if (win != None) {
        long *prop = get_property (win, server.atom [_NET_WM_PID], XA_CARDINAL, NULL);
        fprintf( stderr, RED "tint2: another systray is running, cannot use systray");
        if (prop)
            fprintf( stderr, ": pid=%li", *(long *)prop);
        fprintf(stderr, RESET "\n");
        XFree( prop);
        return;
    }

    // init systray protocol
    net_sel_win = XCreateSimpleWindow(server.display, server.root_win, -1, -1, 1, 1, 0, 0, 0);
    fprintf(stderr, "tint2: systray window %ld\n", net_sel_win);

    // v0.3 trayer specification. tint2 always horizontal.
    // Vertical panel will draw the systray horizontal.
    long orientation = 0;
    XChangeProperty(server.display, net_sel_win,
                    server.atom [_NET_SYSTEM_TRAY_ORIENTATION],
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *)&orientation,
                    1);
    if (systray.icon_size > 0) {
        long icon_size = systray.icon_size;
        XChangeProperty(server.display, net_sel_win,
                        server.atom [_NET_SYSTEM_TRAY_ICON_SIZE],
                        XA_CARDINAL,
                        32,
                        PropModeReplace,
                        (unsigned char *)&icon_size,
                        1);
    }
    long padding = 0;
    XChangeProperty(server.display, net_sel_win,
                    server.atom [_NET_SYSTEM_TRAY_PADDING],
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *)&padding,
                    1);
    long pid = getpid();
    XChangeProperty(server.display, net_sel_win,
                    server.atom [_NET_WM_PID],
                    XA_CARDINAL,
                    32,
                    PropModeReplace,
                    (unsigned char *)&pid,
                    1);

    VisualID vid = XVisualIDFromVisual (systray_composited ? server.visual32 : server.visual);

    XChangeProperty(server.display, net_sel_win,
                    XInternAtom(server.display, "_NET_SYSTEM_TRAY_VISUAL", False),
                    XA_VISUALID,
                    32,
                    PropModeReplace,
                    (unsigned char *)&vid,
                    1);

    XSetSelectionOwner(server.display, server.atom [_NET_SYSTEM_TRAY_SCREEN], net_sel_win, CurrentTime);
    if (XGetSelectionOwner(server.display, server.atom [_NET_SYSTEM_TRAY_SCREEN]) != net_sel_win) {
        stop_net();
        fprintf(stderr, RED "tint2: cannot find systray manager" RESET "\n");
        return;
    }

    fprintf(stderr, GREEN "tint2: systray started" RESET "\n");
    if (systray_profile)
        fprintf(stderr, "tint2: [%f] %s:%d\n", profiling_get_time(), __func__, __LINE__);
    XClientMessageEvent ev = {
        .type = ClientMessage,
        .window = server.root_win,
        .message_type = server.atom [MANAGER],
        .format = 32,
        .data.l = { CurrentTime, server.atom [_NET_SYSTEM_TRAY_SCREEN], net_sel_win, 0, 0 },
    };
    XSendEvent(server.display, server.root_win, False, StructureNotifyMask, (XEvent *)&ev);
}

void handle_systray_event(XClientMessageEvent *e)
{
    if (systray_profile)
        fprintf(stderr, "tint2: [%f] %s:%d\n", profiling_get_time(), __func__, __LINE__);

    Window win;
    unsigned long opcode = e->data.l[1];
    switch (opcode) {
    case SYSTEM_TRAY_REQUEST_DOCK:
        win = e->data.l[2];
        if (win)
            add_icon(win);
        break;

    case SYSTEM_TRAY_BEGIN_MESSAGE:
    case SYSTEM_TRAY_CANCEL_MESSAGE:
        // we don't show baloons messages.
        break;

    default:
        if (opcode == server.atom [_NET_SYSTEM_TRAY_MESSAGE_DATA])
            fprintf(stderr, "tint2: message from dockapp: %s\n", e->data.b);
        else
            fprintf(stderr, RED "tint2: SYSTEM_TRAY : unknown message type" RESET "\n");
        break;
    }
}

void stop_net()
{
    if (systray_profile)
        fprintf(stderr, "tint2: [%f] %s:%d\n", profiling_get_time(), __func__, __LINE__);

    // remove_icon change systray.list_icons
    while (systray.list_icons)
    {
        remove_icon((TrayWindow *)systray.list_icons->data, false);
    }

    if (net_sel_win != None) {
        XDestroyWindow(server.display, net_sel_win);
        net_sel_win = None;
    }
}

unsigned char error;
int window_error_handler(Display *d, XErrorEvent *e)
{
    if (systray_profile)
        fprintf(stderr, RED "tint2: [%f] %s:%d" RESET "\n", profiling_get_time(), __func__, __LINE__);
    error = e->error_code;
    if (e->error_code != BadWindow)
        fprintf(stderr, RED "tint2: systray: error code %d" RESET "\n", e->error_code);
    return 0;
}

static gint compare_traywindows(gconstpointer a, gconstpointer b)
{
    const TrayWindow *traywin_a = a;
    const TrayWindow *traywin_b = b;

#if 0
    // This breaks pygtk2 StatusIcon with blinking activated
    if (traywin_a->empty && !traywin_b->empty)
        return systray.sort == SYSTRAY_SORT_RIGHT2LEFT ? -1 : 1;
    if (!traywin_a->empty && traywin_b->empty)
        return systray.sort == SYSTRAY_SORT_RIGHT2LEFT ? 1 : -1;
#endif

    switch (systray.sort) {
    case SYSTRAY_SORT_ASCENDING:    return g_ascii_strncasecmp(traywin_a->name, traywin_b->name, -1);
    case SYSTRAY_SORT_DESCENDING:   return -g_ascii_strncasecmp(traywin_a->name, traywin_b->name, -1);
    case SYSTRAY_SORT_LEFT2RIGHT:   return traywin_a->chrono - traywin_b->chrono;
    case SYSTRAY_SORT_RIGHT2LEFT:   return traywin_b->chrono - traywin_a->chrono;
    default:                        return 0;
    }
}

void print_icons()
{
    fprintf(stderr, "tint2: systray.list_icons: \n");
    for (GSList *l = systray.list_icons; l; l = l->next) {
        TrayWindow *t = l->data;
        fprintf(stderr, "tint2: %s\n", t->name);
    }
    fprintf(stderr, "tint2: systray.list_icons order: \n");
    for (GSList *l = systray.list_icons; l; l = l->next)
    {
        if (l->next) {
            TrayWindow *t = l->data;
            TrayWindow *u = l->next->data;
            int cmp = compare_traywindows(t, u);
            fprintf(stderr, "tint2: %s %s %s\n", t->name, cmp < 0 ? "<" : cmp == 0 ? "=" : ">", u->name);
        }
    }
}

bool reject_icon(Window win)
{
    if (systray_hide_name_filter && systray_hide_name_filter[0]) {
        if (!systray_hide_name_regex) {
            systray_hide_name_regex = calloc(1, sizeof(*systray_hide_name_regex));
            if (regcomp(systray_hide_name_regex, systray_hide_name_filter, 0) != 0) {
                fprintf(stderr, RED "tint2: Could not compile regex %s" RESET "\n", systray_hide_name_filter);
                free_and_null(systray_hide_name_regex);
                return false;
            }
        }
        char *name = get_window_name(win);
        if (regexec(systray_hide_name_regex, name, 0, NULL, 0) == 0) {
            fprintf(stderr, GREEN "tint2: Filtering out systray icon '%s'" RESET "\n", name);
            return true;
        }
    }
    return false;
}

bool add_icon(Window win)
{
    // Avoid duplicates
    for (GSList *l = systray.list_icons; l; l = l->next) {
        TrayWindow *other = l->data;

        if (other->win == win)
            return false;

    }

    // Filter out systray_hide_by_icon_name
    if (reject_icon(win))
        return false;

    // Dangerous actions begin
    XSync(server.display, False);
    error = 0;
    XErrorHandler old = XSetErrorHandler(window_error_handler);

    XSelectInput(server.display, win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);

    char *name = get_window_name(win);
    if (systray_profile)
        fprintf(stderr, "tint2: [%f] %s:%d win = %lu (%s)\n", profiling_get_time(), __func__, __LINE__, win, name);
    Panel *panel = systray.area.panel;

    // Get the process ID of the application that created the window
    int pid = 0;
    {
        Atom actual_type;
        int actual_format;
        unsigned long nitems;
        unsigned long bytes_after;
        unsigned char *prop = 0;
        int ret = XGetWindowProperty(server.display,
                                     win,
                                     server.atom [_NET_WM_PID],
                                     0,
                                     1024,
                                     False,
                                     AnyPropertyType,
                                     &actual_type,
                                     &actual_format,
                                     &nitems,
                                     &bytes_after,
                                     &prop);
        if (ret == Success && prop) {
            pid = (int)prop[1] * 256 + prop[0];
            XFree( prop);
        }
    }

    // Create the parent window that will embed the icon
    XWindowAttributes attr;
    if (systray_profile)
        fprintf(stderr, "tint2: XGetWindowAttributes(server.display, win = %ld, &attr)\n", win);
    if (XGetWindowAttributes(server.display, win, &attr) == False) {
        free(name);
        XSelectInput(server.display, win, NoEventMask);

        // Dangerous actions end
        XSync(server.display, False);
        XSetErrorHandler(old);

        return false;
    }

    // Dangerous actions end
    XSync(server.display, False);
    XSetErrorHandler(old);

    unsigned long mask = 0;
    XSetWindowAttributes set_attr;
    Visual *visual = server.visual;
    fprintf(stderr,
            GREEN "add_icon: %lu (%s), pid %d, visual %p, colormap %lu, depth %d, width %d, height %d" RESET "\n",
            win,
            name,
            pid,
            (void*)attr.visual,
            attr.colormap,
            attr.depth,
            attr.width,
            attr.height);
    if (server.disable_transparency) {
        set_attr.background_pixmap = ParentRelative;
        mask = CWBackPixmap;
        if (systray_composited || attr.depth != server.depth) {
            visual = attr.visual;
            set_attr.colormap = attr.colormap;
            mask |= CWColormap;
        }
    } else {
        if (systray_composited || attr.depth != server.depth) {
            visual = attr.visual;
            set_attr.background_pixel = 0;
            set_attr.border_pixel = 0;
            set_attr.colormap = attr.colormap;
            mask = CWColormap | CWBackPixel | CWBorderPixel;
        } else {
            set_attr.background_pixmap = ParentRelative;
            mask = CWBackPixmap;
        }
    }

    if (systray_profile)
        fprintf(stderr, "tint2: XCreateWindow(...)\n");
    Window parent = XCreateWindow(server.display, panel->main_win,
                                  0, 0, systray.icon_size, systray.icon_size, 0,
                                  attr.depth,
                                  InputOutput,
                                  visual,
                                  mask,
                                  &set_attr);

    // Add the icon to the list
    TrayWindow *pTrayWin = calloc( 1, sizeof(TrayWindow));
    pTrayWin->parent = parent;
    pTrayWin->win = win;
    pTrayWin->depth = attr.depth;
    // Reparenting is done at the first paint event when the window is positioned correctly over its empty background,
    // to prevent graphical corruptions in icons with fake transparency
    pTrayWin->pid = pid;
    pTrayWin->name = name;
    pTrayWin->chrono = chrono;
    INIT_TIMER(pTrayWin->render_timer);
    INIT_TIMER(pTrayWin->resize_timer);
    chrono++;

    show(&systray.area);

    systray.list_icons = g_slist_insert_sorted (systray.list_icons, pTrayWin, compare_traywindows);
    // print_icons();

    if (!panel->is_hidden) {
        if (systray_profile)
            fprintf(stderr, "tint2: XMapRaised(server.display, pTrayWin->parent)\n");
        XMapRaised(server.display, pTrayWin->parent);
    }

    if (systray_profile)
        fprintf(stderr, "tint2: [%f] %s:%d\n", profiling_get_time(), __func__, __LINE__);

    // Resize and redraw the systray
    if (systray_profile)
        fprintf(stderr,
                BLUE "[%f] %s:%d trigger resize & redraw" RESET "\n",
                profiling_get_time(),
                __func__, __LINE__);
    systray.area.resize_needed = true;
    panel->area.resize_needed = true;
    schedule_redraw(&systray.area);
    refresh_systray = true;
    return true;
}

bool reparent_icon(TrayWindow *pTrayWin)
{
    if (systray_profile)
    {
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);
    }

    if (pTrayWin->reparented)
    {
        return true;
    }

    // Watch for the icon trying to resize itself / closing again
    XSync(server.display, False);
    error = 0;
    XErrorHandler old = XSetErrorHandler(window_error_handler);
    XWithdrawWindow(server.display, pTrayWin->win, server.screen);
    XReparentWindow(server.display, pTrayWin->win, pTrayWin->parent, 0, 0);

    if (systray_profile)
    {
        fprintf(stderr,
                "XMoveResizeWindow(server.display, pTrayWin->win = %ld, 0, 0, pTrayWin->width = %d, pTrayWin->height = %d)\n",
                pTrayWin->win,
                pTrayWin->width, pTrayWin->height);
    }

    XMoveResizeWindow(server.display, pTrayWin->win, 0, 0, pTrayWin->width, pTrayWin->height);

    // Embed into parent
    XEvent e = {.xclient = {
        .type = ClientMessage,
        .serial = 0,
        .send_event = True,
        .message_type = server.atom [_XEMBED],
        .window = pTrayWin->win,
        .format = 32,
        .data.l = { CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0, pTrayWin->parent, 0 },
    } };

    if (systray_profile)
    {
        fprintf(stderr, "tint2: XSendEvent(server.display, pTrayWin->win, False, NoEventMask, &e)\n");
    }

    XSendEvent(server.display, pTrayWin->win, False, NoEventMask, &e);
    

    XSync(server.display, False);
    XSetErrorHandler(old);
    if (error)
    {
        fprintf(stderr,
                RED "systray %d: cannot embed icon for window %lu (%s) parent %lu pid %d" RESET "\n",
                __LINE__,
                pTrayWin->win, pTrayWin->name, pTrayWin->parent,
                pTrayWin->pid);
        remove_icon(pTrayWin, error == BadWindow);
        return false;
    }

    pTrayWin->reparented = true;

    if (systray_profile)
    {
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);
    }

    return true;
}

bool embed_icon(TrayWindow *pTrayWin)
{
    if (systray_profile)
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);
    if (pTrayWin->embedded)
        return true;

    Panel *panel = systray.area.panel;

    XSync(server.display, False);
    error = 0;
    XErrorHandler old = XSetErrorHandler(window_error_handler);

    // Redirect rendering when using compositing
    if (systray_composited) {
        if (systray_profile)
            fprintf(stderr, "tint2: XDamageCreate(server.display, pTrayWin->parent, XDamageReportRawRectangles)\n");
        pTrayWin->damage = XDamageCreate(server.display, pTrayWin->parent, XDamageReportRawRectangles);
        if (systray_profile)
            fprintf(stderr, "tint2: XCompositeRedirectWindow(server.display, pTrayWin->parent, CompositeRedirectManual)\n");
        XCompositeRedirectWindow(server.display, pTrayWin->parent, CompositeRedirectManual);
    }

    XRaiseWindow(server.display, pTrayWin->win);

    // Make the icon visible
    if (systray_profile)
        fprintf(stderr, "tint2: XMapWindow(server.display, pTrayWin->win)\n");
    XMapWindow(server.display, pTrayWin->win);
    if (!panel->is_hidden) {
        if (systray_profile)
            fprintf(stderr, "tint2: XMapRaised(server.display, pTrayWin->parent)\n");
        XMapRaised(server.display, pTrayWin->parent);
    }

    if (systray_profile)
        fprintf(stderr, "tint2: XSync(server.display, False)\n");
    XSync(server.display, False);
    XSetErrorHandler(old);
    if (error) {
        fprintf(stderr,
                RED "systray %d: cannot embed icon for window %lu (%s) parent %lu pid %d" RESET "\n",
                __LINE__,
                pTrayWin->win, pTrayWin->name, pTrayWin->parent,
                pTrayWin->pid);
        remove_icon(pTrayWin, error == BadWindow);
        return false;
    }

    pTrayWin->embedded = true;

    if (systray_profile)
    {
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);
    }

    return true;
}

void remove_icon(TrayWindow *pTrayWin, bool destroyed)
{
    if (systray_profile)
    {
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);
    }
    Panel *panel = systray.area.panel;

    // remove from our list
    systray.list_icons = g_slist_remove(systray.list_icons, pTrayWin);
    fprintf(stderr, YELLOW "tint2: remove_icon: %lu (%s)" RESET "\n", pTrayWin->win, pTrayWin->name);

    if (!destroyed)
    {
        XSelectInput(server.display, pTrayWin->win, NoEventMask);
    }

    if (pTrayWin->damage)
    {
        XDamageDestroy(server.display, pTrayWin->damage);
    }

    // reparent to root
    XSync(server.display, False);
    error = 0;
    XErrorHandler old = XSetErrorHandler(window_error_handler);
    if (!destroyed)
    {
        XUnmapWindow(server.display, pTrayWin->win);
        XReparentWindow(server.display, pTrayWin->win, server.root_win, 0, 0);
    }

    XDestroyWindow(server.display, pTrayWin->parent);
    XSync(server.display, False);
    XSetErrorHandler(old);
    destroy_timer(&pTrayWin->render_timer);
    destroy_timer(&pTrayWin->resize_timer);
    free(pTrayWin->name);
    if (pTrayWin->image)
    {
        imlib_context_set_image(pTrayWin->image);
        imlib_free_image_and_decache();
    }

    free( pTrayWin);

    // check empty systray
    if (!systray.list_icons)
    {
        hide(&systray.area);
    }

    // Resize and redraw the systray
    if (systray_profile)
    {
        fprintf(stderr,
                BLUE "[%f] %s:%d trigger resize & redraw" RESET "\n",
                profiling_get_time(),
                __func__, __LINE__);
    }

    systray.area.resize_needed = true;
    panel->area.resize_needed = true;
    schedule_redraw(&systray.area);
    refresh_systray = true;
}

void systray_resize_icon(void *t)
{
    TrayWindow *pTrayWin = t;

    unsigned int border_width;
    int xpos, ypos;
    unsigned int width, height, depth;
    Window root;
    if (!XGetGeometry(server.display, pTrayWin->win, &root, &xpos, &ypos, &width, &height, &border_width, &depth))
        return;
    else {
        if (systray_profile)
            fprintf(stderr,
                    "systray_resize_icon win = %ld, w = %d, h = %d\n",
                    pTrayWin->win,
                    pTrayWin->width, pTrayWin->height);
        // This is the obvious thing to do but GTK tray icons do not respect the new size
        if (0) {
            XMoveResizeWindow(server.display, pTrayWin->win, 0, 0, pTrayWin->width, pTrayWin->height);
        }
        // This is similar but GTK tray icons still do not respect the new size
        if (0) {
            XWindowChanges changes;
            changes.x = changes.y = 0;
            changes.width = pTrayWin->width;
            changes.height = pTrayWin->height;
            XConfigureWindow(server.display, pTrayWin->win, CWX | CWY | CWWidth | CWHeight, &changes);
        }
        // This is what WMs send to windows to resize them, the new size must not be ignored.
        // A bit brutal but works with GTK and everything else.
        if (1) {
            XConfigureEvent ev;
            ev.type = ConfigureNotify;
            ev.serial = 0;
            ev.send_event = True;
            ev.event = pTrayWin->win;
            ev.window = pTrayWin->win;
            ev.x = 0;
            ev.y = 0;
            ev.width = pTrayWin->width;
            ev.height = pTrayWin->height;
            ev.border_width = 0;
            ev.above = None;
            ev.override_redirect = False;
            XSendEvent(server.display, pTrayWin->win, False, StructureNotifyMask, (XEvent *)&ev);
        }
        XSync(server.display, False);
    }
}

void systray_reconfigure_event(TrayWindow *pTrayWin, XEvent *e)
{
    if (systray_profile)
        fprintf(stderr,
                "XConfigure event: win = %lu (%s), x = %d, y = %d, w = %d, h = %d\n",
                pTrayWin->win, pTrayWin->name,
                e->xconfigure.x,     e->xconfigure.y,
                e->xconfigure.width, e->xconfigure.height);

    if (!pTrayWin->reparented)
        return;

    if (e->xconfigure.x != 0 || e->xconfigure.width != pTrayWin->width ||
        e->xconfigure.y != 0 || e->xconfigure.height != pTrayWin->height)
    {
        if (pTrayWin->bad_size_counter < max_bad_resize_events) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            struct timespec earliest_resize = add_msec_to_timespec(pTrayWin->time_last_resize, resize_period_threshold);
            if (compare_timespecs(&earliest_resize, &now) > 0)
                // Fast resize, but below the threshold
                pTrayWin->bad_size_counter++;
            else {
                // Slow resize, reset counter
                pTrayWin->time_last_resize.tv_sec = now.tv_sec;
                pTrayWin->time_last_resize.tv_nsec = now.tv_nsec;
                pTrayWin->bad_size_counter = 0;
            }
            if (pTrayWin->bad_size_counter < min_bad_resize_events)
                systray_resize_icon(pTrayWin);
            else
                if (!pTrayWin->resize_timer.enabled_)
                    change_timer(&pTrayWin->resize_timer, true, fast_resize_period, 0, systray_resize_icon, pTrayWin);
        } else {
            if (pTrayWin->bad_size_counter == max_bad_resize_events) {
                pTrayWin->bad_size_counter++;
                fprintf(stderr,
                        RED "Detected resize loop for tray icon %lu (%s), throttling resize events" RESET "\n",
                        pTrayWin->win, pTrayWin->name);
            }
            // Delayed resize
            // FIXME Normally we should force the icon to resize fill_color to the size we resized it to when we
            // embedded it.
            // However this triggers a resize loop in new versions of GTK, which we must avoid.
            if (!pTrayWin->resize_timer.enabled_)
                change_timer(&pTrayWin->resize_timer, true, slow_resize_period, 0, systray_resize_icon, pTrayWin);
            return;
        }
    } else
        // Correct size
        stop_timer(&pTrayWin->resize_timer);

    // Resize and redraw the systray
    if (systray_profile)
        fprintf(stderr,
                BLUE "[%f] %s:%d trigger resize & redraw" RESET "\n",
                profiling_get_time(),
                __func__, __LINE__);
    schedule_panel_redraw();
    refresh_systray = true;
}

void systray_property_notify(TrayWindow *pTrayWin, XEvent *e)
{
    Atom at = e->xproperty.atom;
    if (at == server.atom [WM_NAME])
    {
        free(pTrayWin->name);
        pTrayWin->name = get_window_name(pTrayWin->win);
        if (systray.sort == SYSTRAY_SORT_ASCENDING || systray.sort == SYSTRAY_SORT_DESCENDING)
            systray.list_icons = g_slist_sort(systray.list_icons, compare_traywindows);
            // print_icons();
    }
}

void systray_resize_request_event(TrayWindow *pTrayWin, XEvent *e)
{
    if (systray_profile)
        fprintf(stderr,
                "XResizeRequest event: win = %lu (%s), w = %d, h = %d\n",
                pTrayWin->win, pTrayWin->name,
                e->xresizerequest.width, e->xresizerequest.height);

    if (!pTrayWin->reparented)
        return;

    if (e->xresizerequest.width != pTrayWin->width || e->xresizerequest.height != pTrayWin->height) {
        if (pTrayWin->bad_size_counter < max_bad_resize_events)
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            struct timespec earliest_resize = add_msec_to_timespec(pTrayWin->time_last_resize, resize_period_threshold);
            if (compare_timespecs(&earliest_resize, &now) > 0)
                // Fast resize, but below the threshold
                pTrayWin->bad_size_counter++;
            else {
                // Slow resize, reset counter
                pTrayWin->time_last_resize.tv_sec = now.tv_sec;
                pTrayWin->time_last_resize.tv_nsec = now.tv_nsec;
                pTrayWin->bad_size_counter = 0;
            }
            if (pTrayWin->bad_size_counter < min_bad_resize_events)
                systray_resize_icon(pTrayWin);
            else
                if (!pTrayWin->resize_timer.enabled_)
                    change_timer(&pTrayWin->resize_timer, true, fast_resize_period, 0, systray_resize_icon, pTrayWin);
        }
        else
        {
            if (pTrayWin->bad_size_counter == max_bad_resize_events) {
                pTrayWin->bad_size_counter++;
                fprintf(stderr,
                        RED "Detected resize loop for tray icon %lu (%s), throttling resize events" RESET "\n",
                        pTrayWin->win, pTrayWin->name);
            }
            // Delayed resize
            // FIXME Normally we should force the icon to resize to the size we resized it to when we embedded it.
            // However this triggers a resize loop in some versions of GTK, which we must avoid.
            if (!pTrayWin->resize_timer.enabled_)
                    change_timer(&pTrayWin->resize_timer, true, slow_resize_period, 0, systray_resize_icon, pTrayWin);
            return;
        }
    } else
        // Correct size
        stop_timer(&pTrayWin->resize_timer);

    // Resize and redraw the systray
    if (systray_profile)
        fprintf(stderr,
                BLUE "[%f] %s:%d trigger resize & redraw" RESET "\n",
                profiling_get_time(),
                __func__, __LINE__);
    schedule_panel_redraw();
    refresh_systray = true;
}

void systray_destroy_event(TrayWindow *pTrayWin)
{
    if (systray_profile)
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);
    remove_icon(pTrayWin, true);
}

void systray_render_icon_from_image(TrayWindow *pTrayWin)
{
    if (!pTrayWin->image)
        return;
    XCopyArea(server.display,
              render_background, systray.area.pix, server.gc,
              pTrayWin->x - systray.area.posx,   pTrayWin->y - systray.area.posy,
              pTrayWin->width,                   pTrayWin->height,
              pTrayWin->x - systray.area.posx,   pTrayWin->y - systray.area.posy);
    render_image( pTrayWin->image, systray.area.pix, pTrayWin->x - systray.area.posx, pTrayWin->y - systray.area.posy);
}

void systray_render_icon_composited(void *t)
// we end up in this function only in real transparency mode or if systray_task_asb != 100 0 0
// we made also sure, that we always have a 32 bit visual, i.e. we can safely create 32 bit pixmaps here
{
    TrayWindow *pTrayWin = t;

    if (systray_profile)
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);

    // wine tray icons update whenever mouse is over them, so we limit the updates to 50 ms
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct timespec earliest_render = add_msec_to_timespec(pTrayWin->time_last_render, min_refresh_period);
    if (compare_timespecs(&earliest_render, &now) > 0) {
        pTrayWin->num_fast_renders++;
        if (pTrayWin->num_fast_renders > max_fast_refreshes) {
            change_timer(&pTrayWin->render_timer, true, min_refresh_period, 0, systray_render_icon_composited, pTrayWin);
            if (systray_profile)
                fprintf(stderr,
                        YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering" RESET "\n",
                        profiling_get_time(),
                        __func__, __LINE__,
                        pTrayWin->win, pTrayWin->name);
            return;
        }
    } else {
        pTrayWin->time_last_render.tv_sec = now.tv_sec;
        pTrayWin->time_last_render.tv_nsec = now.tv_nsec;
        pTrayWin->num_fast_renders = 0;
    }

    if (pTrayWin->width == 0 || pTrayWin->height == 0) {
        // reschedule rendering since the geometry information has not yet been processed (can happen on slow cpu)
        change_timer(&pTrayWin->render_timer, true, min_refresh_period, 0, systray_render_icon_composited, pTrayWin);
        if (systray_profile)
            fprintf(stderr,
                    YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering" RESET "\n",
                    profiling_get_time(),
                    __func__, __LINE__,
                    pTrayWin->win, pTrayWin->name);
        return;
    }

    stop_timer(&pTrayWin->render_timer);

    // good systray icons support 32 bit depth, but some icons are still 24 bit.
    // We create a heuristic mask for these icons, i.e. we get the rgb value in the top left corner, and
    // mask out all pixel with the same rgb value

    // Very ugly hack, but somehow imlib2 is not able to get the image from the traywindow itself,
    // so we first render the tray window onto a pixmap, and then we tell imlib2 to use this pixmap as
    // drawable. If someone knows why it does not work with the traywindow itself, please tell me ;)
    Pixmap tmp_pmap = XCreatePixmap(server.display, pTrayWin->win,
                                    pTrayWin->width, pTrayWin->height, 32);
    if (!tmp_pmap)
        goto on_systray_error;

    XRenderPictFormat *f;
    
    switch (pTrayWin->depth) {
    case 24: f = XRenderFindStandardFormat(server.display, PictStandardRGB24);
             break;
    case 32: f = XRenderFindStandardFormat(server.display, PictStandardARGB32);
             break;
    default:
        fprintf(stderr, RED "tint2: Strange tray icon found with depth: %d" RESET "\n", pTrayWin->depth);
        XFreePixmap(server.display, tmp_pmap);
        return;
    }
    XRenderPictFormat *f32 = XRenderFindVisualFormat(server.display, server.visual32);
    if (!f || !f32) {
        XFreePixmap(server.display, tmp_pmap);
        goto on_systray_error;
    }

    XSync(server.display, False);
    error = 0;
    XErrorHandler old = XSetErrorHandler(window_error_handler);

    // if (server.real_transparency)
    // Picture pict_image = XRenderCreatePicture(server.display, pTrayWin->parent, f, 0, 0);
    // reverted Rev 407 because here it's breaking alls icon with systray + xcompmgr
    Picture pict_image = XRenderCreatePicture(server.display, pTrayWin->win, f, 0, 0);
    if (!pict_image) {
        XFreePixmap(server.display, tmp_pmap);
        XSetErrorHandler(old);
        goto on_error;
    }
    Picture pict_drawable =
        XRenderCreatePicture(server.display, tmp_pmap, XRenderFindVisualFormat(server.display, server.visual32), 0, 0);
    if (!pict_drawable) {
        XRenderFreePicture(server.display, pict_image);
        XFreePixmap(server.display, tmp_pmap);
        XSetErrorHandler(old);
        goto on_error;
    }
    XRenderComposite(server.display,
                     PictOpSrc,
                     pict_image,
                     None,
                     pict_drawable,
                     0, 0, 0, 0,
                     0, 0, pTrayWin->width, pTrayWin->height);
    XRenderFreePicture(server.display, pict_image);
    XRenderFreePicture(server.display, pict_drawable);
    // end of the ugly hack and we can continue as before

    imlib_context_set_visual(server.visual32);
    imlib_context_set_colormap(server.colormap32);
    imlib_context_set_drawable(tmp_pmap);
    Imlib_Image image = imlib_create_image_from_drawable(0, 0, 0, pTrayWin->width, pTrayWin->height, 1);
    imlib_context_set_visual(server.visual);
    imlib_context_set_colormap(server.colormap);
    XFreePixmap(server.display, tmp_pmap);
    if (!image) {
        imlib_context_set_visual(server.visual);
        imlib_context_set_colormap(server.colormap);
        XSetErrorHandler(old);
        goto on_error;
    } else {
        if (pTrayWin->image) {
            imlib_context_set_image(pTrayWin->image);
            imlib_free_image_and_decache();
        }
        pTrayWin->image = image;
    }

    imlib_context_set_image(pTrayWin->image);
    // if (pTrayWin->depth == 24)
    // imlib_save_image("/home/thil77/test.jpg");
    imlib_image_set_has_alpha(1);
    DATA32 *data = imlib_image_get_data();
    if (pTrayWin->depth == 24)
        create_heuristic_mask(data, pTrayWin->width, pTrayWin->height);

    if (systray.alpha != 100 || systray.brightness != 0 || systray.saturation != 0)
        adjust_asb(data,
                   pTrayWin->width,
                   pTrayWin->height,
                   systray.alpha / 100.0,
                   systray.saturation / 100.0,
                   systray.brightness / 100.0);
    imlib_image_put_back_data(data);

    systray_render_icon_from_image(pTrayWin);

    if (pTrayWin->damage)
        XDamageSubtract(server.display, pTrayWin->damage, None, None);
    XSync(server.display, False);
    XSetErrorHandler(old);

    if (error)
        goto on_error;

    schedule_panel_redraw();

    if (systray_profile)
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__,
                __LINE__,
                pTrayWin->win, pTrayWin->name);

    return;

on_error:
    fprintf(stderr,
            RED "systray %d: rendering error for icon %lu (%s) pid %d" RESET "\n",
            __LINE__,
            pTrayWin->win, pTrayWin->name,
            pTrayWin->pid);
    return;

on_systray_error:
    fprintf(stderr,
            RED "systray %d: rendering error for icon %lu (%s) pid %d. "
                "Disabling compositing and restarting systray..." RESET "\n",
            __LINE__,
            pTrayWin->win, pTrayWin->name,
            pTrayWin->pid);
    systray_composited = 0;
    stop_net();
    start_net();
    return;
}

void systray_render_icon(void *t)
{
    TrayWindow *pTrayWin = t;
    if (!pTrayWin->reparented || !pTrayWin->embedded) {
        //		if (systray_profile)
        //			fprintf(stderr,
        //			        YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering" RESET "\n",
        //			        profiling_get_time(),
        //			        __func__,
        //			        __LINE__,
        //			        pTrayWin->win,
        //			        pTrayWin->name);
        change_timer(&pTrayWin->render_timer, true, min_refresh_period, 0, systray_render_icon, pTrayWin);
        return;
    }

    if (systray_profile)
        fprintf(stderr,
                "[%f] %s:%d win = %lu (%s)\n",
                profiling_get_time(),
                __func__, __LINE__,
                pTrayWin->win, pTrayWin->name);

    if (systray_composited) {
        XSync(server.display, False);
        error = 0;
        XErrorHandler old = XSetErrorHandler(window_error_handler);

        unsigned int border_width;
        int xpos, ypos;
        unsigned int width, height, depth;
        Window root;
        if (!XGetGeometry(server.display, pTrayWin->win, &root, &xpos, &ypos, &width, &height, &border_width, &depth))
        {
            change_timer(&pTrayWin->render_timer, true, min_refresh_period, 0, systray_render_icon, pTrayWin);
            systray_render_icon_from_image(pTrayWin);
            XSetErrorHandler(old);
            return;
        }
        else if (xpos != 0 || ypos != 0 || width != pTrayWin->width || height != pTrayWin->height)
        {
            change_timer(&pTrayWin->render_timer, true, min_refresh_period, 0, systray_render_icon, pTrayWin);
            systray_render_icon_from_image(pTrayWin);
            if (systray_profile)
                fprintf(stderr,
                        YELLOW "[%f] %s:%d win = %lu (%s) delaying rendering" RESET "\n",
                        profiling_get_time(),
                        __func__, __LINE__,
                        pTrayWin->win, pTrayWin->name);
            XSetErrorHandler(old);
            return;
        }
        XSetErrorHandler(old);
    }

    if (systray_profile)
        fprintf(stderr, "tint2: rendering tray icon\n");

    if (systray_composited)
        systray_render_icon_composited(pTrayWin);
    else {
        // Trigger window repaint
        if (systray_profile)
            fprintf(stderr,
                    "XClearArea(server.display, pTrayWin->parent = %ld, 0, 0, pTrayWin->width, pTrayWin->height, True)\n",
                    pTrayWin->parent);
        XClearArea(server.display, pTrayWin->parent, 0, 0, 0, 0, True);
        if (systray_profile)
            fprintf(stderr,
                    "XClearArea(server.display, pTrayWin->win = %ld, 0, 0, pTrayWin->width, pTrayWin->height, True)\n",
                    pTrayWin->win);
        XClearArea(server.display, pTrayWin->win, 0, 0, 0, 0, True);
    }
}

void refresh_systray_icons()
{
    if (systray_profile)
        fprintf(stderr, BLUE "tint2: [%f] %s:%d" RESET "\n", profiling_get_time(), __func__, __LINE__);
    GSList *l;
    for (l = systray.list_icons; l; l = l->next)
    {
        TrayWindow *pTrayWin = l->data;
        systray_render_icon(pTrayWin);
    }
}

bool systray_on_monitor(int i_monitor, int n_panels)
{
    return (i_monitor == systray_monitor) || (i_monitor == 0 && (systray_monitor >= n_panels || systray_monitor < 0));
}

TrayWindow *systray_find_icon(Window win)
{
    for (GSList *l = systray.list_icons; l; l = l->next)
    {
        TrayWindow *pTrayWin = l->data;
        if (pTrayWin->win == win || pTrayWin->parent == win)
            return pTrayWin;
    }
    return NULL;
}
