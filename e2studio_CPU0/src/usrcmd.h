/**
 * @file usrcmd.h
 * @brief NT-Shell command registration framework
 * @details
 * Defines the command table structure, registration macro, and public API
 * for the NT-Shell command system.
 *
 * To add a new command:
 *   1. Implement a handler function:  static int usrcmd_foo(int argc, char **argv);
 *   2. Add an entry to cmdlist[] in usrcmd.c using the NTSHELL_CMD macro:
 *      NTSHELL_CMD("foo", "Description of foo command", usrcmd_foo),
 *   3. The command will automatically appear in 'help' output.
 *
 * @author CuBeatSystems
 * @author Shinichiro Nakamura
 * @copyright
 * ===============================================================
 * Natural Tiny Shell (NT-Shell) Version 0.3.1
 * ===============================================================
 * Copyright (c) 2010-2016 Shinichiro Nakamura
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef USRCMD_H
#define USRCMD_H

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * Command definition macro for the static command table.
 *
 * Usage in cmdlist[]:
 *   NTSHELL_CMD("name", "description", handler_function),
 *
 * @param name  Command name string (what the user types)
 * @param desc  Help description string (shown in 'help' output)
 * @param func  Command handler function (int func(int argc, char **argv))
 */
#define NTSHELL_CMD(name, desc, func)   { (name), (desc), (func) }

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/**
 * Command handler function type
 * @param argc Number of arguments (including the command name itself)
 * @param argv Array of argument strings
 * @return 0 on success, negative value on error (see CMD_ERR_* in cmd_utils.h)
 */
typedef int (*usrcmd_handler_t)(int argc, char **argv);

/**
 * Command table entry
 * @note Each entry in cmdlist[] represents one shell command.
 *       The 'help' command iterates this table to display all available commands.
 */
typedef struct {
    const char      *cmd;   /**< Command name */
    const char      *desc;  /**< Help description */
    usrcmd_handler_t func;  /**< Command handler function */
} cmd_table_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute a command string from NT-Shell
 * @details Parses the input text into argc/argv using ntopt and dispatches
 *          to the matching command handler in the command table.
 * @param text Command string entered by the user
 * @return Command handler return value, or 0 if command not found
 */
int usrcmd_execute(const char *text);

/**
 * Get the command table and its size
 * @details Allows external modules to query the registered commands.
 *          Used by the 'help' command and potentially by tab completion.
 * @param count Pointer to receive the number of entries in the table
 * @return Pointer to the command table array
 */
const cmd_table_t *usrcmd_get_cmdlist(int *count);

#ifdef __cplusplus
}
#endif

#endif /* USRCMD_H */
