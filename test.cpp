#include <fcntl.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

constexpr size_t GUEST_MEM_SIZE = 0x1000; // 4 KB

int main() {
    // 1. Open /dev/kvm
    int kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    assert(kvm_fd >= 0);

    int api_version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    assert(api_version == KVM_API_VERSION);

    // 2. Create VM
    int vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);
    assert(vm_fd >= 0);

    // 3. Allocate guest memory
    void* guest_mem = mmap(nullptr, GUEST_MEM_SIZE,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                            -1, 0);
    assert(guest_mem != MAP_FAILED);

    // Guest code: HLT
    uint8_t code[] = { 0xf4 };
    memcpy(guest_mem, code, sizeof(code));
    
    // Setup minimal GDT at offset 0x100 in guest memory
    // GDT entries: null, code segment, data segment
    uint64_t* gdt = reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(guest_mem) + 0x100);
    gdt[0] = 0;  // Null descriptor
    // Code segment (index 1, selector 0x8): base=0, limit=0xFFFFFFFF, type=0xb (execute/read), present=1
    gdt[1] = 0x00cf9a000000ffff;  // 32-bit code segment
    // Data segment (index 2, selector 0x10): base=0, limit=0xFFFFFFFF, type=0x3 (read/write), present=1
    gdt[2] = 0x00cf92000000ffff;  // 32-bit data segment

    // 4. Map memory into the VM
    kvm_userspace_memory_region mem_region{};
    mem_region.slot = 0;
    mem_region.guest_phys_addr = 0x0;
    mem_region.memory_size = GUEST_MEM_SIZE;
    mem_region.userspace_addr = reinterpret_cast<uint64_t>(guest_mem);

    assert(ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region) == 0);

    // 5. Create vCPU
    int vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
    assert(vcpu_fd >= 0);

    // 6. Map the shared kvm_run structure
    int mmap_size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    assert(mmap_size > 0);

    kvm_run* run = reinterpret_cast<kvm_run*>(
        mmap(nullptr, mmap_size,
             PROT_READ | PROT_WRITE,
             MAP_SHARED, vcpu_fd, 0));
    assert(run != MAP_FAILED);

    // 7. Setup segment registers (required for x86)
    kvm_sregs sregs{};
    assert(ioctl(vcpu_fd, KVM_GET_SREGS, &sregs) == 0);
    
    // Setup GDT register to point to our GDT at guest address 0x100
    sregs.gdt.base = 0x100;
    sregs.gdt.limit = 23;  // 3 entries * 8 bytes - 1
    
    // Setup code segment for 32-bit protected mode (selector 0x8 = index 1, RPL 0)
    sregs.cs.selector = 0x8;
    sregs.cs.base = 0;
    sregs.cs.limit = 0xFFFFFFFF;
    sregs.cs.type = 0xb;  // Execute/Read
    sregs.cs.present = 1;
    sregs.cs.dpl = 0;
    sregs.cs.db = 1;  // 32-bit segment
    sregs.cs.s = 1;   // Code/data segment
    sregs.cs.l = 0;   // Not 64-bit mode
    sregs.cs.g = 1;   // 4KB granularity
    
    // Setup data segments (selector 0x10 = index 2, RPL 0)
    sregs.ds = sregs.es = sregs.fs = sregs.gs = sregs.ss = sregs.cs;
    sregs.ds.selector = 0x10;
    sregs.ds.type = 0x3;  // Read/Write for data segments
    sregs.es.selector = 0x10;
    sregs.es.type = 0x3;
    sregs.fs.selector = 0x10;
    sregs.fs.type = 0x3;
    sregs.gs.selector = 0x10;
    sregs.gs.type = 0x3;
    sregs.ss.selector = 0x10;
    sregs.ss.type = 0x3;
    
    // Setup control registers - protected mode without paging for simplicity
    sregs.cr0 = 0x00000011;  // PE (protected mode), ET
    sregs.cr4 = 0;
    sregs.efer = 0;
    
    assert(ioctl(vcpu_fd, KVM_SET_SREGS, &sregs) == 0);

    // 8. Setup general registers
    kvm_regs regs{};
    regs.rip = 0x0;   // Start executing at guest address 0
    regs.rflags = 0x2;
    regs.rax = 0;
    regs.rbx = 0;
    regs.rcx = 0;
    regs.rdx = 0;
    regs.rsi = 0;
    regs.rdi = 0;
    regs.rsp = 0x1000;  // Stack pointer

    assert(ioctl(vcpu_fd, KVM_SET_REGS, &regs) == 0);

    // 9. Run the VM
    std::cout << "Running VM...\n";

    assert(ioctl(vcpu_fd, KVM_RUN, 0) == 0);

    if (run->exit_reason == KVM_EXIT_HLT) {
        std::cout << "VM halted successfully 🎉\n";
    } else {
        std::cout << "Unexpected exit reason: " << run->exit_reason << "\n";
    }

    return 0;
}