//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2017-2026 MNE-CPP Authors
 *
 * @file     command_pattern.h
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
 *           Gabriel Motta <gabrielbenmotta@gmail.com>
 * @since    0.1.0
 * @date     September 2017
 * @brief    Minimal @c ICommand interface of the command pattern behind the real-time command classes.
 *
 * @ref COMLIB::Command and @ref COMLIB::RawCommand implement it to model the control requests exchanged with
 * mne_rt_server (start or stop a measurement, set parameters, request status). Invokers and receivers are
 * expressed through Qt signals and slots rather than dedicated classes.
 */

#ifndef COMLIB_COMMAND_PATTERN_H
#define COMLIB_COMMAND_PATTERN_H

//=============================================================================================================
// DEFINE NAMESPACE COMLIB
//=============================================================================================================

namespace COMLIB
{

//=============================================================================================================
/**
 * @brief Base of every command: an action that can be queued and executed later.
 *
 * @snippet ex_com/main.cpp command_pattern_usage
 */
class ICommand
{
public:
    virtual ~ICommand() = default;

    //=========================================================================================================
    /**
     * Performs the action.
     */
    virtual void execute() = 0;
};

} // namespace COMLIB

#endif // COMLIB_COMMAND_PATTERN_H
