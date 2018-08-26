/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/dpms.h>
#include <X11/keysym.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/vt.h>
#include <pwd.h>
#include <shadow.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

static void die(const char* errstr, ...)
{
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

/* only run as root */
static const char* get_password()
{
    const char* rval;

    if (geteuid() != 0) {
        die("sflock: cannot retrieve password entry (make sure to suid sflock)\n");
    }

    struct spwd* sp;
    sp = getspnam(getenv("USER"));
    if (sp == NULL) {
        die("sflock: an error occured or no entries were found\n");
    }
    endspent();
    rval = sp->sp_pwdp;

    return rval;
}

int main(int argc, char** argv)
{
    char curs[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    char buf[32], passwd[256];
    int num, screen, width, height, term, pid;

    const char* pws;
    unsigned int len;
    bool running = true, update = false, sleepmode = false;
    Cursor invisible;
    Display* dpy;
    KeySym ksym;
    Pixmap pmap;
    Window root, w;
    XColor black, red, dummy;
    XEvent ev;
    XSetWindowAttributes wa;
    GC gc;
    XGCValues values;

    /* disable tty switching */
    if ((term = open("/dev/console", O_RDWR)) == -1) {
        perror("error opening console");
    }

    if ((ioctl(term, VT_LOCKSWITCH)) == -1) {
        perror("error locking console");
    }

    /* deamonize */
    pid = fork();
    if (pid < 0) {
        die("Could not fork sflock.");
    }
    if (pid > 0) {
        exit(0); // exit parent
    }

    pws = get_password();

    if (!(dpy = XOpenDisplay(0))) {
        die("sflock: cannot open dpy\n");
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    width = DisplayWidth(dpy, screen);
    height = DisplayHeight(dpy, screen);

    wa.override_redirect = 1;
    wa.background_pixel = XBlackPixel(dpy, screen);
    w = XCreateWindow(dpy, root, 0, 0, width, height, 0,
        DefaultDepth(dpy, screen), CopyFromParent,
        DefaultVisual(dpy, screen),
        CWOverrideRedirect | CWBackPixel, &wa);

    XAllocNamedColor(dpy, DefaultColormap(dpy, screen), "orange red", &red,
        &dummy);
    XAllocNamedColor(dpy, DefaultColormap(dpy, screen), "black", &black, &dummy);
    pmap = XCreateBitmapFromData(dpy, w, curs, 8, 8);
    invisible = XCreatePixmapCursor(dpy, pmap, pmap, &black, &black, 0, 0);
    XDefineCursor(dpy, w, invisible);
    XMapRaised(dpy, w);

    gc = XCreateGC(dpy, w, (unsigned long)0, &values);
    XSetForeground(dpy, gc, XWhitePixel(dpy, screen));

    for (len = 1000; len; len--) {
        if (XGrabPointer(dpy, root, false,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeAsync, GrabModeAsync, None, invisible,
                CurrentTime)
            == GrabSuccess) {
            break;
        }
        usleep(1000);
    }
    if ((running = running && (len > 0))) {
        for (len = 1000; len; len--) {
            if (XGrabKeyboard(dpy, root, true, GrabModeAsync, GrabModeAsync,
                    CurrentTime)
                == GrabSuccess) {
                break;
            }
            usleep(1000);
        }
        running = (len > 0);
    }

    len = 0;
    XSync(dpy, false);
    update = false;
    sleepmode = false;

    /* main event loop */
    while (running && !XNextEvent(dpy, &ev)) {
        if (sleepmode) {
            DPMSEnable(dpy);
            DPMSForceLevel(dpy, DPMSModeOff);
            XFlush(dpy);
        }

        if (update) {
            XClearWindow(dpy, w);
            update = false;
        }

        if (ev.type == MotionNotify) {
            sleepmode = false;
        }

        if (ev.type == KeyPress) {
            sleepmode = false;

            buf[0] = 0;
            num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
            if (IsKeypadKey(ksym)) {
                if (ksym == XK_KP_Enter) {
                    ksym = XK_Return;
                } else if (ksym >= XK_KP_0 && ksym <= XK_KP_9) {
                    ksym = (ksym - XK_KP_0) + XK_0;
                }
            }
            if (IsFunctionKey(ksym) || IsKeypadKey(ksym) || IsMiscFunctionKey(ksym) || IsPFKey(ksym) || IsPrivateKeypadKey(ksym)) {
                continue;
            }

            switch (ksym) {
            case XK_Return:
                passwd[len] = 0;
                running = strcmp(crypt(passwd, pws), pws);
                if (running != 0) {
                    // change background on wrong password
                    XSetWindowBackground(dpy, w, red.pixel);
                }
                len = 0;
                break;
            case XK_Escape:
                len = 0;

                if (DPMSCapable(dpy)) {
                    sleepmode = true;
                }

                break;
            case XK_BackSpace:
                if (len) {
                    --len;
                }
                break;
            default:
                if (num && !iscntrl((int)buf[0]) && (len + num < sizeof passwd)) {
                    memcpy(passwd + len, buf, num);
                    len += num;
                }

                break;
            }

            update = true; // show changes
        }
    }

    /* free and unlock */
    int err = setreuid(geteuid(), 0);
    if (err == -1) {
        printf("Oh dear, something went wrong: %s\n", strerror(errno));
    }
    if ((ioctl(term, VT_UNLOCKSWITCH)) == -1) {
        perror("error unlocking console");
    }

    close(term);
    err = setuid(getuid()); // drop rights permanently
    if (err == -1) {
        printf("Oh dear, something went wrong: %s\n", strerror(errno));
    }

    XUngrabPointer(dpy, CurrentTime);
    XFreePixmap(dpy, pmap);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    return 0;
}
