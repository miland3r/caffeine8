/*
 * Copyright (C) 2023 Ulrich van Brakel
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <dbus/dbus.h>
#include <X11/Xutil.h>
#include "caffeine8.h"

namespace caffeine8
{
    const std::string BANNER_IMAGE_PATH = DEFAULT_BANNER_IMAGE_PATH;
    const std::string TITLE_IMAGE_PATH = DEFAULT_TITLE_IMAGE_PATH;
    const std::string pidFilePath = "/tmp/caffeine8.pid";
    const std::string statusFilePath = "/tmp/caffeine8.status";
    const std::string VERSION = "1.1.0"; // Version property
    std::string lastQbusError = "NONE";  // Global variable for last status/error message

    namespace
    {
        struct InhibitorHandles
        {
            DBusConnection *sessionConnection = nullptr;
            DBusConnection *systemConnection = nullptr;
            uint32_t screenSaverCookie = 0;
            int idleFd = -1;
            int sleepFd = -1;
        };

        InhibitorHandles inhibitorHandles;
        bool debugLoggingEnabled = false;
        bool inhibitorsActive = false;
        volatile sig_atomic_t terminationRequested = 0;
        volatile sig_atomic_t acquireRequested = 0;
        volatile sig_atomic_t releaseRequested = 0;

        constexpr const char *APP_NAME = "caffeine8";
        constexpr const char *SCREEN_SAVER_OBJECT = "/ScreenSaver";
        constexpr const char *SCREEN_SAVER_INTERFACE = "org.freedesktop.ScreenSaver";
        constexpr const char *SCREEN_SAVER_BUS = "org.freedesktop.ScreenSaver";
        constexpr const char *LOGIN1_OBJECT = "/org/freedesktop/login1";
        constexpr const char *LOGIN1_INTERFACE = "org.freedesktop.login1.Manager";
        constexpr const char *LOGIN1_BUS = "org.freedesktop.login1";

        void logDebug(const std::string &message)
        {
            if (debugLoggingEnabled)
            {
                std::cout << "[debug] " << message << std::endl;
            }
        }

        std::string sanitizeStatusMessage(const std::string &message)
        {
            std::string sanitized = message;
            std::replace(sanitized.begin(), sanitized.end(), '\n', ' ');
            std::replace(sanitized.begin(), sanitized.end(), '\r', ' ');
            return sanitized;
        }

        void updateStatusFile()
        {
            std::ofstream statusFile(statusFilePath, std::ios::trunc);
            if (!statusFile.is_open())
            {
                logDebug("Unable to open status file for writing: " + statusFilePath);
                return;
            }

            statusFile << "pid=" << getpid() << "\n";
            statusFile << "active=" << (inhibitorsActive ? 1 : 0) << "\n";
            statusFile << "debug=" << (debugLoggingEnabled ? 1 : 0) << "\n";
            statusFile << "message=" << sanitizeStatusMessage(lastQbusError) << "\n";
        }

        void setStatusMessage(const std::string &message)
        {
            lastQbusError = message;
            logDebug(message);
            updateStatusFile();
        }

        void handleSignal(int sig)
        {
            switch (sig)
            {
            case SIGTERM:
            case SIGINT:
                terminationRequested = 1;
                break;
            case SIGUSR1:
                acquireRequested = 1;
                break;
            case SIGUSR2:
                releaseRequested = 1;
                break;
            default:
                break;
            }
        }

        DBusConnection *getConnection(DBusBusType type, const char *label)
        {
            DBusError err;
            dbus_error_init(&err);
            DBusConnection *connection = dbus_bus_get(type, &err);
            if (dbus_error_is_set(&err))
            {
                std::string errorMessage = std::string("Failed to connect to ") + label + " bus: " + err.message;
                setStatusMessage(errorMessage);
                dbus_error_free(&err);
                return nullptr;
            }
            if (!connection)
            {
                setStatusMessage(std::string("Failed to obtain ") + label + " DBus connection (nullptr).");
            }
            return connection;
        }

        bool acquireScreenSaverInhibitor()
        {
            if (!inhibitorHandles.sessionConnection)
            {
                inhibitorHandles.sessionConnection = getConnection(DBUS_BUS_SESSION, "session");
                if (!inhibitorHandles.sessionConnection)
                {
                    return false;
                }
            }

            DBusMessage *message = dbus_message_new_method_call(SCREEN_SAVER_BUS,
                                                                SCREEN_SAVER_OBJECT,
                                                                SCREEN_SAVER_INTERFACE,
                                                                "Inhibit");
            if (!message)
            {
                setStatusMessage("Failed to create DBus message for ScreenSaver.Inhibit.");
                return false;
            }

            const char *reason = "caffeine8 prevents automatic locking";
            dbus_message_append_args(message,
                                     DBUS_TYPE_STRING, &APP_NAME,
                                     DBUS_TYPE_STRING, &reason,
                                     DBUS_TYPE_INVALID);

            DBusError err;
            dbus_error_init(&err);
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(inhibitorHandles.sessionConnection, message, -1, &err);
            dbus_message_unref(message);

            if (dbus_error_is_set(&err))
            {
                std::string errorMessage = std::string("ScreenSaver.Inhibit failed: ") + err.message;
                setStatusMessage(errorMessage);
                dbus_error_free(&err);
                return false;
            }

            if (!reply)
            {
                setStatusMessage("ScreenSaver.Inhibit returned null reply.");
                return false;
            }

            uint32_t cookie = 0;
            if (!dbus_message_get_args(reply, &err,
                                       DBUS_TYPE_UINT32, &cookie,
                                       DBUS_TYPE_INVALID))
            {
                std::string errorMessage = std::string("Unable to parse ScreenSaver.Inhibit reply: ") + err.message;
                setStatusMessage(errorMessage);
                dbus_error_free(&err);
                dbus_message_unref(reply);
                return false;
            }

            inhibitorHandles.screenSaverCookie = cookie;
            dbus_message_unref(reply);

            logDebug("Screen saver inhibitor acquired. Cookie=" + std::to_string(cookie));
            return true;
        }

        bool acquireLogin1Inhibitor(const char *what, int &fdOut)
        {
            if (!inhibitorHandles.systemConnection)
            {
                inhibitorHandles.systemConnection = getConnection(DBUS_BUS_SYSTEM, "system");
                if (!inhibitorHandles.systemConnection)
                {
                    return false;
                }
            }

            DBusMessage *message = dbus_message_new_method_call(LOGIN1_BUS,
                                                                LOGIN1_OBJECT,
                                                                LOGIN1_INTERFACE,
                                                                "Inhibit");
            if (!message)
            {
                setStatusMessage(std::string("Failed to create DBus message for login1.Inhibit ") + what + ".");
                return false;
            }

            const char *why = "caffeine8 is preventing automatic sleep";
            const char *mode = "block";
            dbus_message_append_args(message,
                                     DBUS_TYPE_STRING, &what,
                                     DBUS_TYPE_STRING, &APP_NAME,
                                     DBUS_TYPE_STRING, &why,
                                     DBUS_TYPE_STRING, &mode,
                                     DBUS_TYPE_INVALID);

            DBusError err;
            dbus_error_init(&err);
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(inhibitorHandles.systemConnection, message, -1, &err);
            dbus_message_unref(message);

            if (dbus_error_is_set(&err))
            {
                std::string errorMessage = std::string("login1.Inhibit(") + what + ") failed: " + err.message;
                setStatusMessage(errorMessage);
                dbus_error_free(&err);
                return false;
            }

            if (!reply)
            {
                setStatusMessage(std::string("login1.Inhibit(") + what + ") returned null reply.");
                return false;
            }

            DBusMessageIter iter;
            if (!dbus_message_iter_init(reply, &iter))
            {
                setStatusMessage(std::string("login1.Inhibit(") + what + ") reply has no arguments.");
                dbus_message_unref(reply);
                return false;
            }

            int type = dbus_message_iter_get_arg_type(&iter);
            if (type != DBUS_TYPE_UNIX_FD)
            {
                setStatusMessage(std::string("login1.Inhibit(") + what + ") reply is not a UNIX FD.");
                dbus_message_unref(reply);
                return false;
            }

            dbus_message_iter_get_basic(&iter, &fdOut);
            dbus_message_unref(reply);

            logDebug(std::string("systemd inhibitor for ") + what + " acquired. FD=" + std::to_string(fdOut));
            return true;
        }

        void releaseScreenSaverInhibitor()
        {
            if (!inhibitorHandles.screenSaverCookie || !inhibitorHandles.sessionConnection)
            {
                return;
            }

            DBusMessage *message = dbus_message_new_method_call(SCREEN_SAVER_BUS,
                                                                SCREEN_SAVER_OBJECT,
                                                                SCREEN_SAVER_INTERFACE,
                                                                "UnInhibit");
            if (!message)
            {
                logDebug("Failed to create DBus message for ScreenSaver.UnInhibit.");
                return;
            }

            uint32_t cookie = inhibitorHandles.screenSaverCookie;
            dbus_message_append_args(message,
                                     DBUS_TYPE_UINT32, &cookie,
                                     DBUS_TYPE_INVALID);

            DBusError err;
            dbus_error_init(&err);
            DBusMessage *reply = dbus_connection_send_with_reply_and_block(inhibitorHandles.sessionConnection, message, -1, &err);
            dbus_message_unref(message);
            if (reply)
            {
                dbus_message_unref(reply);
            }

            if (dbus_error_is_set(&err))
            {
                std::string errorMessage = std::string("ScreenSaver.UnInhibit failed: ") + err.message;
                logDebug(errorMessage);
                dbus_error_free(&err);
            }

            inhibitorHandles.screenSaverCookie = 0;
            logDebug("Screen saver inhibitor released.");
        }

        void releaseLogin1Fd(int &fdRef, const char *what)
        {
            if (fdRef >= 0)
            {
                close(fdRef);
                logDebug(std::string("systemd inhibitor for ") + what + " released.");
                fdRef = -1;
            }
        }

        void cleanupInhibitors()
        {
            releaseScreenSaverInhibitor();
            releaseLogin1Fd(inhibitorHandles.idleFd, "idle");
            releaseLogin1Fd(inhibitorHandles.sleepFd, "sleep");
            inhibitorsActive = false;

            if (inhibitorHandles.sessionConnection)
            {
                dbus_connection_unref(inhibitorHandles.sessionConnection);
                inhibitorHandles.sessionConnection = nullptr;
            }

            if (inhibitorHandles.systemConnection)
            {
                dbus_connection_unref(inhibitorHandles.systemConnection);
                inhibitorHandles.systemConnection = nullptr;
            }

            updateStatusFile();
        }

        bool acquireInhibitors()
        {
            bool screen = acquireScreenSaverInhibitor();
            bool idle = acquireLogin1Inhibitor("idle", inhibitorHandles.idleFd);
            bool sleep = acquireLogin1Inhibitor("sleep", inhibitorHandles.sleepFd);

            inhibitorsActive = screen && idle && sleep;
            if (inhibitorsActive)
            {
                setStatusMessage("Inhibitors active (screen saver, idle, sleep).");
                return true;
            }

            updateStatusFile();
            return false;
        }
    } // namespace

    bool checkExistingInstance(pid_t &existingPid)
    {
        std::ifstream pidFile(pidFilePath);
        if (pidFile.is_open())
        {
            pidFile >> existingPid;
            pidFile.close();
            if (kill(existingPid, 0) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void writePidFile(pid_t pid)
    {
        std::ofstream pidFile(pidFilePath);
        if (pidFile.is_open())
        {
            pidFile << pid;
            pidFile.close();
        }
        else
        {
            std::cerr << "Could not write PID file." << std::endl;
        }
    }

    void deletePidFile()
    {
        if (remove(pidFilePath.c_str()) != 0)
        {
            std::cerr << "Could not delete PID file." << std::endl;
        }
    }

    void showUI(pid_t targetPid)
    {
        Display *display = XOpenDisplay(NULL);
        if (display == NULL)
        {
            std::cerr << "Cannot open display" << std::endl;
            return;
        }

        int screen = DefaultScreen(display);
        Window root = RootWindow(display, screen);
        Window win = XCreateSimpleWindow(display, root, 10, 10, 900, 320, 1, BlackPixel(display, screen), BlackPixel(display, screen));

        XSelectInput(display, win, ExposureMask | KeyPressMask | StructureNotifyMask);
        XMapWindow(display, win);

        XEvent ev;
        Pixmap banner_pixmap;
        XpmAttributes banner_attributes;
        banner_attributes.valuemask = 0;
        Pixmap title_pixmap;
        XpmAttributes title_attributes;
        title_attributes.valuemask = 0;

        GC gc = XCreateGC(display, win, 0, NULL);

        if (XpmReadFileToPixmap(display, win, DEFAULT_BANNER_IMAGE_PATH, &banner_pixmap, NULL, &banner_attributes) != XpmSuccess)
        {
            std::cerr << "Cannot read Banner XPM file directly" << std::endl;
            XFreeGC(display, gc);
            XDestroyWindow(display, win);
            XCloseDisplay(display);
            return;
        }

        if (XpmReadFileToPixmap(display, win, DEFAULT_TITLE_IMAGE_PATH, &title_pixmap, NULL, &title_attributes) != XpmSuccess)
        {
            std::cerr << "Cannot read Title XPM file directly" << std::endl;
            XFreePixmap(display, banner_pixmap);
            XFreeGC(display, gc);
            XDestroyWindow(display, win);
            XCloseDisplay(display);
            return;
        }

        XImage *banner = XGetImage(display, banner_pixmap, 0, 0, banner_attributes.width, banner_attributes.height, AllPlanes, ZPixmap);
        XImage *title = XGetImage(display, title_pixmap, 0, 0, title_attributes.width, title_attributes.height, AllPlanes, ZPixmap);

        if (!banner || !title)
        {
            std::cerr << "Failed to create images from XPM resources." << std::endl;
            if (banner)
            {
                XDestroyImage(banner);
            }
            if (title)
            {
                XDestroyImage(title);
            }
            XFreePixmap(display, banner_pixmap);
            XFreePixmap(display, title_pixmap);
            XFreeGC(display, gc);
            XDestroyWindow(display, win);
            XCloseDisplay(display);
            return;
        }

        struct UiStatus
        {
            bool hasData = false;
            bool active = false;
            bool debug = false;
            pid_t pid = -1;
            std::string message = "Awaiting status update...";
        };

        auto readStatus = []() -> UiStatus
        {
            UiStatus info;
            std::ifstream statusFile(statusFilePath);
            if (!statusFile.is_open())
            {
                info.message = "Status file not found.";
                return info;
            }

            info.hasData = true;
            std::string line;
            while (std::getline(statusFile, line))
            {
                auto pos = line.find('=');
                if (pos == std::string::npos)
                {
                    continue;
                }
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                if (key == "active")
                {
                    info.active = (value == "1" || value == "true" || value == "TRUE");
                }
                else if (key == "debug")
                {
                    info.debug = (value == "1" || value == "true" || value == "TRUE");
                }
                else if (key == "pid")
                {
                    char *end = nullptr;
                    long parsed = std::strtol(value.c_str(), &end, 10);
                    if (end != value.c_str())
                    {
                        info.pid = static_cast<pid_t>(parsed);
                    }
                }
                else if (key == "message")
                {
                    info.message = value;
                }
            }
            if (info.message.empty())
            {
                info.message = "Status file present but empty.";
            }
            return info;
        };

        UiStatus currentStatus = readStatus();
        std::string uiMessage;

        auto render = [&](bool reloadStatus)
        {
            if (reloadStatus)
            {
                currentStatus = readStatus();
            }

            XWindowAttributes attrs;
            XGetWindowAttributes(display, win, &attrs);
            int win_width = attrs.width;
            int win_height = attrs.height;

            XSetForeground(display, gc, BlackPixel(display, screen));
            XFillRectangle(display, win, gc, 0, 0, win_width, win_height);

            float x_scale = static_cast<float>(win_width) / banner_attributes.width;
            float y_scale = static_cast<float>(win_height) / banner_attributes.height;
            float scale = std::min(x_scale, y_scale);
            scale = std::max(scale, 0.2f);

            int scaled_width = std::max(1, static_cast<int>(banner_attributes.width * scale));
            int scaled_height = std::max(1, static_cast<int>(banner_attributes.height * scale));

            XImage *scaled_image = XCreateImage(display,
                                                DefaultVisual(display, screen),
                                                banner->depth,
                                                ZPixmap,
                                                0,
                                                NULL,
                                                scaled_width,
                                                scaled_height,
                                                32,
                                                0);

            if (scaled_image)
            {
                scaled_image->data = static_cast<char *>(malloc(scaled_image->bytes_per_line * scaled_height));
            }

            if (scaled_image && scaled_image->data)
            {
                float x_ratio = static_cast<float>(banner_attributes.width) / static_cast<float>(scaled_width);
                float y_ratio = static_cast<float>(banner_attributes.height) / static_cast<float>(scaled_height);

                for (int y = 0; y < scaled_height; ++y)
                {
                    for (int x = 0; x < scaled_width; ++x)
                    {
                        int px = static_cast<int>(x * x_ratio);
                        int py = static_cast<int>(y * y_ratio);
                        XPutPixel(scaled_image, x, y, XGetPixel(banner, px, py));
                    }
                }

                XPutImage(display, win, gc, scaled_image, 0, 0, 0, 0, scaled_width, scaled_height);

                free(scaled_image->data);
                scaled_image->data = NULL;
                XDestroyImage(scaled_image);
            }

            int x = scaled_width + 20;
            int y = 70;
            int line_height = 20;

            XPutImage(display, win, gc, title, 0, 0, x, 0, title_attributes.width, title_attributes.height);
            XSetForeground(display, gc, WhitePixel(display, screen));

            std::ostringstream oss;
            oss << "version " << VERSION;
            oss << "\n\nTarget PID: " << (targetPid > 0 ? std::to_string(targetPid) : std::string("N/A"));
            if (currentStatus.hasData)
            {
                oss << "\nLoop PID: " << (currentStatus.pid > 0 ? std::to_string(currentStatus.pid) : std::string("unknown"));
                oss << "\nInhibitors: " << (currentStatus.active ? "ACTIVE" : "inactive");
                oss << "\nDebug mode: " << (currentStatus.debug ? "enabled" : "disabled");
                oss << "\nStatus: " << currentStatus.message;
            }
            else
            {
                oss << "\nStatus: " << currentStatus.message;
            }

            oss << "\n\nControls:";
            oss << "\n  T - toggle inhibitors";
            oss << "\n  R - refresh status";
            oss << "\n  Q - quit UI";
            oss << "\n  Ctrl+D - close window";

            if (!uiMessage.empty())
            {
                oss << "\n\nLast action: " << uiMessage;
            }

            std::istringstream iss(oss.str());
            std::string line;
            while (std::getline(iss, line))
            {
                XDrawString(display, win, gc, x, y, line.c_str(), line.length());
                y += line_height;
            }

            XFlush(display);
        };

        bool running = true;
        while (running)
        {
            XNextEvent(display, &ev);
            if (ev.type == Expose || ev.type == ConfigureNotify)
            {
                render(true);
            }
            else if (ev.type == KeyPress)
            {
                KeySym keysym = XLookupKeysym(&ev.xkey, 0);
                bool controlDown = (ev.xkey.state & ControlMask) != 0;

                if (controlDown && keysym == XK_d)
                {
                    running = false;
                }
                else if (keysym == XK_q || keysym == XK_Q || keysym == XK_Escape)
                {
                    running = false;
                }
                else if (keysym == XK_r || keysym == XK_R)
                {
                    uiMessage = "Status refreshed.";
                    render(true);
                }
                else if (keysym == XK_t || keysym == XK_T)
                {
                    if (targetPid > 0)
                    {
                        int signalToSend = currentStatus.active ? SIGUSR2 : SIGUSR1;
                        if (kill(targetPid, signalToSend) == 0)
                        {
                            uiMessage = currentStatus.active ? "Toggle requested: release inhibitors." : "Toggle requested: acquire inhibitors.";
                            usleep(300000);
                            render(true);
                        }
                        else
                        {
                            uiMessage = "Failed to signal caffeine8 process.";
                            render(false);
                        }
                    }
                    else
                    {
                        uiMessage = "No active caffeine8 process.";
                        render(false);
                    }
                }
            }
        }

        XDestroyImage(banner);
        XDestroyImage(title);
        XFreePixmap(display, banner_pixmap);
        XFreePixmap(display, title_pixmap);
        XFreeGC(display, gc);
        XDestroyWindow(display, win);
        XCloseDisplay(display);
    }

    bool isDebugMode()
    {
        return debugLoggingEnabled;
    }

    void setDebugMode(bool enabled)
    {
        debugLoggingEnabled = enabled;
        if (enabled)
        {
            logDebug("Debug logging enabled.");
        }
    }

    void runInhibitorLoop()
    {
        if (!acquireInhibitors())
        {
            logDebug("Initial inhibitor acquisition failed.");
        }

        struct sigaction action {};
        action.sa_handler = handleSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGUSR1, &action, nullptr);
        sigaction(SIGUSR2, &action, nullptr);

        while (!terminationRequested)
        {
            if (acquireRequested)
            {
                acquireRequested = 0;
                if (!inhibitorsActive)
                {
                    if (!acquireInhibitors())
                    {
                        logDebug("Acquire request failed; inhibitors remain inactive.");
                    }
                }
                else
                {
                    logDebug("Acquire request ignored; inhibitors already active.");
                    updateStatusFile();
                }
            }

            if (releaseRequested)
            {
                releaseRequested = 0;
                if (inhibitorsActive)
                {
                    cleanupInhibitors();
                    setStatusMessage("Inhibitors released by user request.");
                }
                else
                {
                    setStatusMessage("Inhibitors already inactive.");
                }
            }

            sleep(1);
        }

        logDebug("Termination requested, cleaning up inhibitors.");
        bool wereActive = inhibitorsActive;
        cleanupInhibitors();
        if (wereActive)
        {
            setStatusMessage("Inhibitors released (process exiting).");
        }
    }

} // namespace caffeine8

int main(int argc, char *argv[])
{
    pid_t existingPid = -1;

    bool debugRequested = false;
    std::string command;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--debug")
        {
            debugRequested = true;
        }
        else if (command.empty())
        {
            command = arg;
        }
        else
        {
            std::cerr << "Too many arguments provided." << std::endl;
            return 1;
        }
    }

    if (command.empty())
    {
        command = "start";
    }

    caffeine8::setDebugMode(debugRequested);

    if (command == "stop")
    {
        if (caffeine8::checkExistingInstance(existingPid))
        {
            std::cout << "Stopping existing instance with PID " << existingPid << std::endl;
            kill(existingPid, SIGTERM);
            caffeine8::deletePidFile();
        }
        else
        {
            std::cout << "No existing instance found." << std::endl;
        }
        return 0;
    }

    if (command == "attach")
    {
        if (!caffeine8::checkExistingInstance(existingPid))
        {
            std::cout << "Warning: caffeine8 is not running. Starting it now." << std::endl;
            pid_t pid = fork();
            if (pid > 0)
            {
                caffeine8::writePidFile(pid);
                existingPid = pid;
            }
            else if (pid < 0)
            {
                std::cerr << "Fork failed while starting caffeine8 for attach." << std::endl;
                return 1;
            }
            else if (pid == 0)
            {
                caffeine8::runInhibitorLoop();
                return 0;
            }
        }
        Magick::InitializeMagick(NULL);
        caffeine8::showUI(existingPid);
        return 0;
    }

    if (command != "start")
    {
        std::cerr << "Invalid argument. Use 'start', 'stop', or 'attach'." << std::endl;
        return 1;
    }

    if (caffeine8::checkExistingInstance(existingPid))
    {
        std::cout << "An instance of caffeine8 is already running with PID " << existingPid << ". Killing it." << std::endl;
        kill(existingPid, SIGTERM);
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        std::cerr << "Fork failed." << std::endl;
        return 1;
    }

    if (pid > 0)
    {
        caffeine8::writePidFile(pid);
        std::cout << "New instance of caffeine8 started with PID " << pid << std::endl;
        return 0;
    }

    caffeine8::runInhibitorLoop();
    return 0;
}
