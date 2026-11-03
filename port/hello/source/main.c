/* M0 toolchain check: a minimal wut console app built to .rpx and .wuhb. */
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    WHBProcInit();
    WHBLogConsoleInit();
    WHBLogPrintf("DreamKartsU toolchain check: hello from wut.");

    while (WHBProcIsRunning()) {
        WHBLogConsoleDraw();
        OSSleepTicks(OSMillisecondsToTicks(100));
    }

    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
