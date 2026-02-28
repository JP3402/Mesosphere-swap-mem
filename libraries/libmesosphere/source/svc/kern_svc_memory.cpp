/*
 * Copyright (c) Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <mesosphere.hpp>

namespace ams::kern::svc {

    /* =============================    Common    ============================= */

    namespace {

        constexpr bool IsValidSetMemoryPermission(ams::svc::MemoryPermission perm) {
            switch (perm) {
                case ams::svc::MemoryPermission_None:
                case ams::svc::MemoryPermission_Read:
                case ams::svc::MemoryPermission_ReadWrite:
                    return true;
                default:
                    return false;
            }
        }

        Result SetMemoryPermission(uintptr_t address, size_t size, ams::svc::MemoryPermission perm) {
            /* Validate address / size. */
            R_UNLESS(util::IsAligned(address, PageSize), svc::ResultInvalidAddress());
            R_UNLESS(util::IsAligned(size,    PageSize), svc::ResultInvalidSize());
            R_UNLESS(size > 0,                           svc::ResultInvalidSize());
            R_UNLESS((address < address + size),         svc::ResultInvalidCurrentMemory());

            /* Validate the permission. */
            R_UNLESS(IsValidSetMemoryPermission(perm), svc::ResultInvalidNewMemoryPermission());

            /* Validate that the region is in range for the current process. */
            auto &page_table = GetCurrentProcess().GetPageTable();
            R_UNLESS(page_table.Contains(address, size), svc::ResultInvalidCurrentMemory());

            /* Set the memory attribute. */
            R_RETURN(page_table.SetMemoryPermission(address, size, perm));
        }

        Result SetMemoryAttribute(uintptr_t address, size_t size, uint32_t mask, uint32_t attr) {
            /* Validate address / size. */
            R_UNLESS(util::IsAligned(address, PageSize), svc::ResultInvalidAddress());
            R_UNLESS(util::IsAligned(size,    PageSize), svc::ResultInvalidSize());
            R_UNLESS(size > 0,                           svc::ResultInvalidSize());
            R_UNLESS((address < address + size),         svc::ResultInvalidCurrentMemory());

            /* Validate the attribute and mask. */
            constexpr u32 SupportedMask = ams::svc::MemoryAttribute_Uncached | ams::svc::MemoryAttribute_PermissionLocked;
            R_UNLESS((mask | attr) == mask,                          svc::ResultInvalidCombination());
            R_UNLESS((mask | attr | SupportedMask) == SupportedMask, svc::ResultInvalidCombination());

            /* Check that permission locked is either being set or not masked. */
            R_UNLESS((mask & ams::svc::MemoryAttribute_PermissionLocked) == (attr & ams::svc::MemoryAttribute_PermissionLocked), svc::ResultInvalidCombination());

            /* Validate that the region is in range for the current process. */
            auto &page_table = GetCurrentProcess().GetPageTable();
            R_UNLESS(page_table.Contains(address, size), svc::ResultInvalidCurrentMemory());

            /* Set the memory attribute. */
            R_RETURN(page_table.SetMemoryAttribute(address, size, mask, attr));
        }

        Result MapMemory(uintptr_t dst_address, uintptr_t src_address, size_t size) {
            /* Validate that addresses are page aligned. */
            R_UNLESS(util::IsAligned(dst_address, PageSize), svc::ResultInvalidAddress());
            R_UNLESS(util::IsAligned(src_address, PageSize), svc::ResultInvalidAddress());

            /* Validate that size is positive and page aligned. */
            R_UNLESS(size > 0,                        svc::ResultInvalidSize());
            R_UNLESS(util::IsAligned(size, PageSize), svc::ResultInvalidSize());

            /* Ensure that neither mapping overflows. */
            R_UNLESS(src_address < src_address + size, svc::ResultInvalidCurrentMemory());
            R_UNLESS(dst_address < dst_address + size, svc::ResultInvalidCurrentMemory());

            /* Get the page table we're operating on. */
            auto &page_table = GetCurrentProcess().GetPageTable();

            /* Ensure that the memory we're mapping is in range. */
            R_UNLESS(page_table.Contains(src_address, size),                       svc::ResultInvalidCurrentMemory());
            R_UNLESS(page_table.CanContain(dst_address, size, KMemoryState_Stack), svc::ResultInvalidMemoryRegion());

            /* Map the memory. */
            R_RETURN(page_table.MapMemory(dst_address, src_address, size));
        }

        Result UnmapMemory(uintptr_t dst_address, uintptr_t src_address, size_t size) {
            /* Validate that addresses are page aligned. */
            R_UNLESS(util::IsAligned(dst_address, PageSize), svc::ResultInvalidAddress());
            R_UNLESS(util::IsAligned(src_address, PageSize), svc::ResultInvalidAddress());

            /* Validate that size is positive and page aligned. */
            R_UNLESS(size > 0,                        svc::ResultInvalidSize());
            R_UNLESS(util::IsAligned(size, PageSize), svc::ResultInvalidSize());

            /* Ensure that neither mapping overflows. */
            R_UNLESS(src_address < src_address + size, svc::ResultInvalidCurrentMemory());
            R_UNLESS(dst_address < dst_address + size, svc::ResultInvalidCurrentMemory());

            /* Get the page table we're operating on. */
            auto &page_table = GetCurrentProcess().GetPageTable();

            /* Ensure that the memory we're unmapping is in range. */
            R_UNLESS(page_table.Contains(src_address, size),                       svc::ResultInvalidCurrentMemory());
            R_UNLESS(page_table.CanContain(dst_address, size, KMemoryState_Stack), svc::ResultInvalidMemoryRegion());

            /* Unmap the memory. */
            R_RETURN(page_table.UnmapMemory(dst_address, src_address, size));
        }

        Result GetSwapRequest(uint64_t *out_process_id, uint64_t *out_thread_id, ams::svc::Address *out_vaddr) {
            KScopedSchedulerLock sl;

            /* Dequeue the next thread. */
            KThread *thread = g_SwapRequestListHead;
            R_UNLESS(thread != nullptr, svc::ResultNotFound());

            g_SwapRequestListHead = thread->GetSwapNext();
            if (g_SwapRequestListHead == nullptr) {
                g_SwapRequestListTail = nullptr;
            }
            thread->SetSwapNext(nullptr);

            /* Fill in the request info. */
            *out_process_id = thread->GetOwnerProcess()->GetId();
            *out_thread_id  = thread->GetId();
			auto temp_addr = thread->GetSwapVirtualAddress();
            *out_vaddr = ams::svc::Address(*reinterpret_cast<const u64*>(&temp_addr));

            /* Close the queue's reference to the thread. */
            thread->Close();

            R_SUCCEED();
        }

        Result MarkAsResidentAndWake(uint64_t process_id, uint64_t thread_id, ams::svc::Address vaddr, ams::svc::PhysicalAddress paddr) {
            /* Get the process from ID. */
            KProcess *process = KProcess::GetProcessFromId(process_id);
            R_UNLESS(process != nullptr, svc::ResultInvalidHandle());
            ON_SCOPE_EXIT { process->Close(); };

            /* Get the thread from ID. */
            KThread *thread = KThread::GetThreadFromId(thread_id);
            R_UNLESS(thread != nullptr, svc::ResultInvalidHandle());
            ON_SCOPE_EXIT { thread->Close(); };

            /* Ensure the thread is owned by the process. */
            R_UNLESS(thread->GetOwnerProcess() == process, svc::ResultInvalidHandle());

            /* Ensure the fault address matches. */
            R_UNLESS(thread->GetSwapVirtualAddress() == vaddr, svc::ResultInvalidAddress());

            /* Mark as resident and wake. */
			auto temp_vaddr = vaddr;
            R_RETURN(process->GetPageTable().GetPageTableImpl().MarkAsResidentAndWake(KProcessAddress(*reinterpret_cast<const u64*>(&temp_vaddr)), KPhysicalAddress(paddr), thread));
        }

        Result RegisterSwapEvent(ams::svc::Handle event_handle) {
            /* Get the event from its handle. */
            KScopedAutoObject event = GetCurrentProcess().GetHandleTable().GetObject<KEvent>(event_handle);
            R_UNLESS(event.IsNotNull(), svc::ResultInvalidHandle());

            /* Set the global swap event. */
            if (g_SwapEvent != nullptr) {
                g_SwapEvent->Close();
            }
            g_SwapEvent = event.ReleasePointerUnsafe();

            R_SUCCEED();
        }

    }

    /* =============================    64 ABI    ============================= */

    Result SetMemoryPermission64(ams::svc::Address address, ams::svc::Size size, ams::svc::MemoryPermission perm) {
        R_RETURN(SetMemoryPermission(address, size, perm));
    }

    Result SetMemoryAttribute64(ams::svc::Address address, ams::svc::Size size, uint32_t mask, uint32_t attr) {
        R_RETURN(SetMemoryAttribute(address, size, mask, attr));
    }

    Result MapMemory64(ams::svc::Address dst_address, ams::svc::Address src_address, ams::svc::Size size) {
        R_RETURN(MapMemory(dst_address, src_address, size));
    }

    Result UnmapMemory64(ams::svc::Address dst_address, ams::svc::Address src_address, ams::svc::Size size) {
        R_RETURN(UnmapMemory(dst_address, src_address, size));
    }

    /* ============================= 64From32 ABI ============================= */

    Result SetMemoryPermission64From32(ams::svc::Address address, ams::svc::Size size, ams::svc::MemoryPermission perm) {
        R_RETURN(SetMemoryPermission(address, size, perm));
    }

    Result SetMemoryAttribute64From32(ams::svc::Address address, ams::svc::Size size, uint32_t mask, uint32_t attr) {
        R_RETURN(SetMemoryAttribute(address, size, mask, attr));
    }

    Result MapMemory64From32(ams::svc::Address dst_address, ams::svc::Address src_address, ams::svc::Size size) {
        R_RETURN(MapMemory(dst_address, src_address, size));
    }

    Result UnmapMemory64From32(ams::svc::Address dst_address, ams::svc::Address src_address, ams::svc::Size size) {
        R_RETURN(UnmapMemory(dst_address, src_address, size));
    }

    Result GetSwapRequest64(uint64_t *out_process_id, uint64_t *out_thread_id, ams::svc::Address *out_vaddr) {
        R_RETURN(GetSwapRequest(out_process_id, out_thread_id, out_vaddr));
    }

    Result GetSwapRequest64From32(uint64_t *out_process_id, uint64_t *out_thread_id, ams::svc::Address *out_vaddr) {
        R_RETURN(GetSwapRequest(out_process_id, out_thread_id, out_vaddr));
    }

    Result MarkAsResidentAndWake64(uint64_t process_id, uint64_t thread_id, ams::svc::Address vaddr, ams::svc::PhysicalAddress paddr) {
        R_RETURN(MarkAsResidentAndWake(process_id, thread_id, vaddr, paddr));
    }

    Result MarkAsResidentAndWake64From32(uint64_t process_id, uint64_t thread_id, ams::svc::Address vaddr, ams::svc::PhysicalAddress paddr) {
        R_RETURN(MarkAsResidentAndWake(process_id, thread_id, vaddr, paddr));
    }

    Result RegisterSwapEvent64(ams::svc::Handle event_handle) {
        R_RETURN(RegisterSwapEvent(event_handle));
    }

    Result RegisterSwapEvent64From32(ams::svc::Handle event_handle) {
        R_RETURN(RegisterSwapEvent(event_handle));
    }

}
