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

#ifndef CAFFEINE_H
#define CAFFEINE_H

#include <string>
#include <unistd.h>
#include <chrono>
#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/keysym.h>
#include <Magick++.h>
#include "config.h"

namespace caffeine8
{

    /// @brief Path to the PID file.
    extern const std::string pidFilePath;

    /// @brief Path to the status file shared with the UI.
    extern const std::string statusFilePath;

    /// @brief Last status or error message exposed to the UI.
    extern std::string lastQbusError;

    /// @brief Version of the application.
    extern const std::string VERSION;

    /**
     * @brief Path to the default banner image.
     *
     * This variable holds the file system path to the banner image used in the application.
     * It is set by CMake during the build process.
     */
    extern const std::string BANNER_IMAGE_PATH;

    /**
     * @brief Path to the default title image.
     *
     * This variable holds the file system path to the title image used in the application.
     * It is set by CMake during the build process.
     */
    extern const std::string TITLE_IMAGE_PATH;

    /**
     * @brief Checks for an existing instance of the application.
     *
     * @param existingPid Reference to a pid_t variable to store the PID of the existing instance.
     * @return true if an existing instance is found, false otherwise.
     */
    bool checkExistingInstance(pid_t &existingPid);

    /**
     * @brief Writes the PID to a file.
     *
     * @param pid The PID to write to the file.
     */
    void writePidFile(pid_t pid);

    /**
     * @brief Deletes the PID file.
     */
    void deletePidFile();

    /**
     * @brief Shows the UI of the application.
     *
     * @param targetPid PID of the inhibitor process the UI should control.
     */
    void showUI(pid_t targetPid);

    /**
     * @brief Enables or disables verbose debug logging.
     *
     * @param enabled True to enable debug output.
     */
    void setDebugMode(bool enabled);

    /**
     * @brief Returns true when debug logging is active.
     */
    bool isDebugMode();

    /**
     * @brief Runs the inhibitor loop in the background process.
     */
    void runInhibitorLoop();

} // namespace caffeine8

#endif // CAFFEINE_H
