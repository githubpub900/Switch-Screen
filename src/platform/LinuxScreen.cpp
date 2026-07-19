#include "../ScreenManager.hpp"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <unistd.h>
#include <vector>
#include <string>

namespace selectscreen {
namespace {
    struct DisplayHandle {
        Display* display = XOpenDisplay(nullptr);
        ~DisplayHandle() { if (display) XCloseDisplay(display); }
    };

    Window findWindow(Display* dpy, Window root, pid_t pid) {
        Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", True);
        Window rootRet, parentRet;
        Window* children = nullptr;
        unsigned int count = 0;
        if (!XQueryTree(dpy, root, &rootRet, &parentRet, &children, &count)) return 0;

        Window found = 0;
        for (unsigned int i = 0; i < count && !found; ++i) {
            if (pidAtom != None) {
                Atom actualType;
                int actualFormat;
                unsigned long nitems, bytesAfter;
                unsigned char* data = nullptr;
                if (XGetWindowProperty(
                        dpy,
                        children[i],
                        pidAtom,
                        0,
                        1,
                        False,
                        XA_CARDINAL,
                        &actualType,
                        &actualFormat,
                        &nitems,
                        &bytesAfter,
                        &data
                    ) == Success && data) {
                    if (*reinterpret_cast<unsigned long*>(data) == static_cast<unsigned long>(pid)) {
                        found = children[i];
                    }
                    XFree(data);
                }
            }
            if (!found) found = findWindow(dpy, children[i], pid);
        }

        if (children) XFree(children);
        return found;
    }

    bool isFullscreen(Display* dpy, Window win) {
        Atom stateAtom = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom fullscreenAtom = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        Atom actualType;
        int actualFormat;
        unsigned long nitems, bytesAfter;
        unsigned char* data = nullptr;

        if (XGetWindowProperty(
                dpy,
                win,
                stateAtom,
                0,
                1024,
                False,
                XA_ATOM,
                &actualType,
                &actualFormat,
                &nitems,
                &bytesAfter,
                &data
            ) != Success || !data) {
            return false;
        }

        bool found = false;
        auto atoms = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == fullscreenAtom) {
                found = true;
                break;
            }
        }
        XFree(data);
        return found;
    }

    void sendFullscreen(Display* dpy, Window win, bool enabled) {
        Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = win;
        event.xclient.message_type = state;
        event.xclient.format = 32;
        event.xclient.data.l[0] = enabled ? 1 : 0;
        event.xclient.data.l[1] = fullscreen;
        event.xclient.data.l[3] = 1;
        XSendEvent(
            dpy,
            DefaultRootWindow(dpy),
            False,
            SubstructureRedirectMask | SubstructureNotifyMask,
            &event
        );
    }
}

std::vector<ScreenInfo> ScreenManager::enumerate() {
    DisplayHandle handle;
    std::vector<ScreenInfo> out;
    if (!handle.display) return out;

    Window root = DefaultRootWindow(handle.display);
    XRRScreenResources* resources = XRRGetScreenResourcesCurrent(handle.display, root);
    if (!resources) return out;

    RROutput primary = XRRGetOutputPrimary(handle.display, root);
    for (int i = 0; i < resources->noutput; ++i) {
        XRROutputInfo* output = XRRGetOutputInfo(handle.display, resources, resources->outputs[i]);
        if (!output || output->connection != RR_Connected || output->crtc == 0) {
            if (output) XRRFreeOutputInfo(output);
            continue;
        }

        XRRCrtcInfo* crtc = XRRGetCrtcInfo(handle.display, resources, output->crtc);
        int refresh = 0;
        for (int m = 0; m < resources->nmode; ++m) {
            if (resources->modes[m].id == crtc->mode &&
                resources->modes[m].hTotal &&
                resources->modes[m].vTotal) {
                refresh = static_cast<int>(
                    resources->modes[m].dotClock /
                    double(resources->modes[m].hTotal * resources->modes[m].vTotal) + 0.5
                );
                break;
            }
        }

        out.push_back(ScreenInfo{
            static_cast<int>(out.size()),
            std::string(output->name, output->nameLen),
            crtc->x,
            crtc->y,
            static_cast<int>(crtc->width),
            static_cast<int>(crtc->height),
            refresh,
            resources->outputs[i] == primary
        });

        XRRFreeCrtcInfo(crtc);
        XRRFreeOutputInfo(output);
    }

    XRRFreeScreenResources(resources);
    return out;
}

bool ScreenManager::apply(ApplyOptions const& options, std::string& error) {
    auto screens = enumerate();
    if (options.screenIndex < 0 || options.screenIndex >= static_cast<int>(screens.size())) {
        error = "The selected screen index is out of range.";
        return false;
    }

    DisplayHandle handle;
    if (!handle.display) {
        error = "Unable to connect to the X11 display server.";
        return false;
    }

    Window win = findWindow(handle.display, DefaultRootWindow(handle.display), getpid());
    if (!win) {
        error = "Geometry Dash's X11 window could not be found.";
        return false;
    }

    auto const& screen = screens[options.screenIndex];
    XWindowAttributes attrs{};
    XGetWindowAttributes(handle.display, win, &attrs);
    bool fullscreen = isFullscreen(handle.display, win);

    if (fullscreen) {
        sendFullscreen(handle.display, win, false);
        XSync(handle.display, False);
        XMoveResizeWindow(handle.display, win, screen.x, screen.y, screen.width, screen.height);
        sendFullscreen(handle.display, win, true);
    } else {
        int x = screen.x + (screen.width - attrs.width) / 2;
        int y = screen.y + (screen.height - attrs.height) / 2;
        XMoveWindow(handle.display, win, x, y);
    }

    XRaiseWindow(handle.display, win);
    XFlush(handle.display);
    return true;
}

}
