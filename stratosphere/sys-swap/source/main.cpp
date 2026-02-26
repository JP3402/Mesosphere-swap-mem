/*
 * sys-swap: Virtualized System Memory (Swap) Daemon
 */
#include <stratosphere.hpp>

namespace ams {

    namespace init {
        void InitializeSystemModule() {
            /* Initialize SDMMC for raw access. */
            sdmmc::Initialize(sdmmc::Port_SdCard0);
        }
        void FinalizeSystemModule() {
            sdmmc::Finalize(sdmmc::Port_SdCard0);
        }
        void Startup() { /* ... */ }
    }

    void NORETURN Exit(int rc) {
        AMS_UNUSED(rc);
        AMS_ABORT("Exit called by sys-swap");
    }

    /* Dummy structure for swap requests. */
    struct SwapRequest {
        u64 process_id;
        uptr vaddr;
        u64 sector_offset;
        os::NativeHandle thread_handle;
    };

    void Main() {
        os::SetThreadNamePointer(os::GetCurrentThread(), "sys-swap.Main");

        /* 1. Initialize SD card. */
        R_ABORT_UNLESS(sdmmc::Activate(sdmmc::Port_SdCard0));

        /* 2. Main loop. */
        while (true) {
            /* TODO: Wait for KEvent from kernel. */
            // os::WaitEvent(g_SwapEvent);

            /* Dummy Logic:
               - Dequeue request.
               - sdmmc::Read(SdCard0, offset, 8 sectors (4KB), buffer);
               - SvcMapMemory(target_process, resident_page, vaddr, 4096);
               - SvcResumeThread(thread_handle);
            */
            
            os::SleepThread(TimeSpan::FromSeconds(1));
        }
    }
}
