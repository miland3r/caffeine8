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

#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include "caffeine8.h"

namespace caffeine8
{
    const std::string BANNER_IMAGE_PATH = DEFAULT_BANNER_IMAGE_PATH;
    const std::string TITLE_IMAGE_PATH = DEFAULT_TITLE_IMAGE_PATH;
    const std::string pidFilePath = "/tmp/caffeine8.pid";
    const std::string VERSION = "1.0.0"; // Version property
    std::string lastQbusError = "NONE";  // Global variable for last qbus error

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

    void showUI()
    {
        Display *display = XOpenDisplay(NULL);
        if (display == NULL)
        {
            std::cerr << "Cannot open display" << std::endl;
            return;
        }

        int screen = DefaultScreen(display);
        Window root = RootWindow(display, screen);
        Window win = XCreateSimpleWindow(display, root, 10, 10, 900, 290, 1, BlackPixel(display, screen), BlackPixel(display, screen));

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
            return;
        }

        if (XpmReadFileToPixmap(display, win, DEFAULT_TITLE_IMAGE_PATH, &title_pixmap, NULL, &title_attributes) != XpmSuccess)
        {
            std::cerr << "Cannot read Title XPM file directly" << std::endl;
            return;
        }

        XImage *banner = XGetImage(display, banner_pixmap, 0, 0, banner_attributes.width, banner_attributes.height, AllPlanes, ZPixmap);
        XImage *title = XGetImage(display, title_pixmap, 0, 0, title_attributes.width, title_attributes.height, AllPlanes, ZPixmap);

        pid_t myPid = getpid(); // Get the PID of the current process

        while (true)
        {
            XNextEvent(display, &ev);
            if (ev.type == Expose || ev.type == ConfigureNotify)
            {
                int win_width = ev.xconfigure.width;
                int win_height = ev.xconfigure.height;

                XSetForeground(display, gc, BlackPixel(display, screen));
                XFillRectangle(display, win, gc, 0, 0, win_width, win_height);

                float x_scale = static_cast<float>(win_width) / banner_attributes.width;
                float y_scale = static_cast<float>(win_height) / banner_attributes.height;
                float scale = std::min(x_scale, y_scale);

                int scaled_width = static_cast<int>(banner_attributes.width * scale);
                int scaled_height = static_cast<int>(banner_attributes.height * scale);

                XImage *scaled_image = XCreateImage(display, DefaultVisual(display, screen), banner->depth, ZPixmap, 0, NULL, scaled_width, scaled_height, 32, 0);
                scaled_image->data = (char *)malloc(scaled_image->bytes_per_line * scaled_height);

                float x_ratio = (float)banner_attributes.width / (float)scaled_width;
                float y_ratio = (float)banner_attributes.height / (float)scaled_height;

                for (int y = 0; y < scaled_height; ++y)
                {
                    for (int x = 0; x < scaled_width; ++x)
                    {
                        int px = (int)(x * x_ratio);
                        int py = (int)(y * y_ratio);
                        XPutPixel(scaled_image, x, y, XGetPixel(banner, px, py));
                    }
                }

                XPutImage(display, win, gc, scaled_image, 0, 0, 0, 0, scaled_width, scaled_height);

                free(scaled_image->data);
                scaled_image->data = NULL;
                XDestroyImage(scaled_image);

                int line_height = 20;      // Height of each line in pixels
                int x = scaled_width + 20; // X position where text starts
                int y = 70;                // Initial Y position where text starts

                XPutImage(display, win, gc, title, 0, 0, x, 0, title_attributes.width, title_attributes.height);

                XSetForeground(display, gc, WhitePixel(display, screen)); // Set text color to white

                // Draw the title

                // Draw the version and other info
                std::string text = "version " + VERSION;
                text += "\n\nPID: " + std::to_string(myPid);
                text += "\nErrors: " + lastQbusError;
                text += "\n\nPress CTRL + D to close this window.";

                std::istringstream iss(text);
                std::string line;
                while (std::getline(iss, line))
                {
                    XDrawString(display, win, gc, x, y, line.c_str(), line.length());
                    y += line_height; // Move down for the next line
                }
            }
            if (ev.type == KeyPress)
            {
                if (ev.xkey.keycode == XKeysymToKeycode(display, XK_d) && (ev.xkey.state & ControlMask))
                {
                    break;
                }
            }
        }

        XDestroyImage(banner);
        XDestroyImage(title);
        XFreePixmap(display, banner_pixmap);
        XFreePixmap(display, title_pixmap);
        XDestroyWindow(display, win);
        XCloseDisplay(display);
    }

} // namespace caffeine8

int main(int argc, char *argv[])
{
    pid_t existingPid;

    if (argc > 1)
    {
        std::string arg = argv[1];
        if (arg == "stop")
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
        else if (arg == "attach")
        {
            if (!caffeine8::checkExistingInstance(existingPid))
            {
                std::cout << "Warning: caffeine8 is not running. Starting it now." << std::endl;
                pid_t pid = fork();
                if (pid > 0)
                {
                    caffeine8::writePidFile(pid);
                }
                else if (pid == 0)
                {
                    while (true)
                    {
                        system("qdbus org.freedesktop.ScreenSaver /ScreenSaver SimulateUserActivity > /dev/null");
                        sleep(60);
                    }
                }
            }
            Magick::InitializeMagick(NULL);
            caffeine8::showUI();
            return 0;
        }
        else if (arg == "start")
        {
        }
        else
        {
            std::cerr << "Invalid argument. Use 'start', 'stop', 'attach', or 'detach'." << std::endl;
            return 1;
        }
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

    if (pid == 0)
    {
        while (true)
        {
            std::string errorOutput;
            FILE *fp = popen("qdbus org.freedesktop.ScreenSaver /ScreenSaver SimulateUserActivity 2>&1", "r");
            if (fp == NULL)
            {
                caffeine8::lastQbusError = "Failed to run qdbus command";
            }
            else
            {
                char buffer[128];
                while (fgets(buffer, sizeof(buffer), fp) != NULL)
                {
                    errorOutput += buffer;
                }
                pclose(fp);
                if (!errorOutput.empty())
                {
                    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    caffeine8::lastQbusError = std::ctime(&now);
                    caffeine8::lastQbusError += ": " + errorOutput;
                }
            }
            sleep(60);
        }
    }

    return 0;
}
