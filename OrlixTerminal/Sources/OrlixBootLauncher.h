#ifndef ORLIX_TERMINAL_BOOT_LAUNCHER_H
#define ORLIX_TERMINAL_BOOT_LAUNCHER_H

int OrlixTerminalBootProfileNamed(const char *profile_name);
const char *OrlixTerminalBootStatusMessage(int status);
void OrlixTerminalInstallConsoleOutputFileDescriptor(int fd);

#endif
