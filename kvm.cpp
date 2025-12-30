/*
Relevant system calls:

queries extensions to the core KVM API. User passes an extension identifier (an int) and receives an int (0 or 1) describing the extension availability
iotctl(vm->fd, KVM_CHECK_EXTENSION, KVM_CAP_*)

Shows the maximum allowed number of memory slots.
KVM_CAP_NR_MEMSLOTS


ioctl(vm->sys_fd, KVM_CREATE_VM, 0)
ioctl(vm->fd, KVM_CREATE_VCPU, 0);
ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0)
ioctl(vm->sys_fd, KVM_GET_API_VERSION, 0)

defines physical address of a three-page region in the guest physical address space.
Region must be within the first 4 GB of the guest physical address space and must not conflict with any existing regions or mmio addresses.
ioctl(vm->fd, KVM_SET_TSS_ADDR, 0xfffbd000)


Allows user to create/modify/delete a guest physical memory slot.
Bits 0-15 of "slot" specify the slot id; should be less than max number of user memory slots supported per VM.
if KVM_CAP_MULTI_ADDRESS_SPACE is supported, bits 16-31 specify the address space id.
ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &struct kvm_userspace_memory_region) 

Runs a guest vcpu
mmap()ing a vcpu fd at offset 0  with the size given by KVM_GET_VCPU_MMAP_SIZE returns a pointer to a struct kvm_run.
ioctl(vcpu->fd, KVM_RUN, 0)

ioctl(vcpu->fd, KVM_GET_REGS, &regs)
ioctl(vcpu->fd, KVM_SET_REGS, &regs)
ioctl(vcpu->fd, KVM_GET_SREGS, &sregs)
ioctl(vcpu->fd, KVM_SET_SREGS, &sregs)

ioctl(vcpu->fd, KVM_SET_CPUID2, &struct kvm_cpuid2)

sets the physical address of a one-page region in the guest physical address space.
Setting address to 0 resets the address to its default (0xfffbc000).
Required on Intel. Fails if KVM_CAP_SET_IDENTITY_MAP_ADDR is not supported or if any vcpu has already been created.
ioctl(vm->fd, KVM_SET_IDENTITY_MAP_ADDR, 0)

Docs: https://docs.kernel.org/6.18/virt/kvm/api.html
*/

/*
Registers:
RIP - entry point address where guest code execution begins
RSP (stack pointer)- point to a valid stack region allocated in guest memory
RFLAGS - 0x2 (bit 1 must be set)
other GPRS - RAX, RBX, RCX usually initialize to 0

CRO - protected mode and paging (CR0_PE | CR0_PG = 0x80000001)
CR3 - set to page table base address
CR4 - enable PAE (CR4_PAE = 0x20) for 64-bit mode
EFER - enable long mode (EFER_LME | EFER_LMA = 0x500)

CS - code segment w/ L bit set (64-bit mode indicator)
DS, ES, SS - data segments
GDT - minlmal global descriptor table
a  typical configuration would have CS with base=0, limit=0xFFFFFFFF, type=0xb (execute/read), L=1 (64-bit), and DPL=0 (kernel mode) or DPL=3 (user mode)
*/

#ifndef KVM_HPP
#define KVM_HPP

#include <iostream>
#include <vector>
#include <unistd.h>
#include <linux/kvm.h>

#define KVM_FD "/dev/kvm"
#define TSS_ADDR 0xfffbd000 // the TSS address (Intel x86)
// #define RAM_SIZE 512000000
// #define VCPU_COUNT 1

/*
ioctl(vm->fd, KVM_CREATE_VCPU, 0);
ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0)
*/
struct vcpu {
    /*
    mmap()ing a vpcu fd returns a pointer to a struct kvm_run.
    */
    vcpu(int vcpu_fd, int vcpu_id) : vcpu_fd(vcpu_fd), vcpu_id(vcpu_id) {};
    virtual ~vcpu() {
        close(vcpu_fd);
    };

    // copy constructor
    vcpu(const vcpu&) = delete;

    // copy assignment
    vcpu& operator=(const vcpu&) = delete;

    // move constructor
    vcpu(vcpu&& other) noexcept :
    vcpu_fd(other.vcpu_fd), 
    vcpu_id(other.vcpu_id), 
    kvm_regs(other.kvm_regs),
    kvm_sregs(other.kvm_sregs),
    kvm_run(other.kvm_run) {
        other.vcpu_fd = -1;
    };

    // move assignment
    vcpu& operator=(vcpu&& other) noexcept {
        if (this != &other) {
            if (vcpu_fd >= 0) close(vcpu_fd);
            vcpu_fd = other.vcpu_fd;
            vcpu_id = other.vcpu_id;
            kvm_regs = other.kvm_regs;
            kvm_sregs = other.kvm_sregs;
            kvm_run = other.kvm_run;

            other.vcpu_fd = -1;
        }
        return *this;
    };

    // KVM_GET_VCPU_MMAP_SIZE system call
    int get_vcpu_mmap_size() {
        return ioctl(vcpu_fd, KVM_GET_VCPU_MMAP_SIZE);
    };
    // KVM_GET_REGS system call
    int get_vcpu_regs() {
        struct kvm_regs regs;
        return ioctl(vcpu_fd, KVM_GET_REGS, &regs);
    };
    // KVM_GET_SREGS system call
    int get_vcpu_sregs() {
        struct kvm_sregs regs;
        return ioctl(vcpu_fd, KVM_GET_SREGS, &regs);
    };
    // KVM_SET_REGS system call
    int set_vcpu_regs(struct kvm_regs* regs) {
        return ioctl(vcpu_fd, KVM_SET_REGS, regs);
    };
    // KVM_SET_SREGS system call
    int set_vcpu_sregs(struct kvm_sregs* sregs) {
        return ioctl(vcpu_fd, KVM_SET_SREGS, sregs);
    }
    // KVM_SET_CPUID system call
    // TODO: fallback to KVM_SET_CPUID if not available on this system
    int set_vcpu_cpuid(struct kvm_cpuid* cpuid) {
        return ioctl(vcpu_fd, KVM_SET_CPUID2, cpuid);
    }
    // KVM_GET_CPUID2 system call
    int get_vcpu_cpuid() {
        return ioctl(vcpu_fd, KVM_GET_CPUID2)
    };

    int vcpu_fd;
    int vcpu_id;
    struct kvm_run* kvm_run;
    struct kvm_regs* kvm_regs;
    struct kvm_sregs* kvm_sregs;
};

// a VM instance created using KVM
struct microVM {
    explicit microVM(int vm_fd);
    virtual ~microVM() {
        close(vm_fd);
    }
    int vm_run();
    int vm_stop();
    int vm_destroy();

    std::vector<vcpu> get_vcpus();
    int add_vcpu(vcpu& vcpu);
    int remove_vcpu(vcpu& vcpu);
    vcpu get_vcpu_by_id(int vcpu_id);
    // gets recommended max vcpu using KVM_CAP_NR_VCPUS of KVM_CHECK_EXTENSION ioctl
    int get_max_vcpu_count() {
        return ioctl(vm_fd, KVM_CHECK_EXTENSION, KVM_CAP_NR_VCPUS);
    }
    
    int vm_fd; // the VM instance
    std::vector<vcpu> vcpus; // the VCPUs
};

struct kvm {
    kvm() {
        dev_fd = open("/dev/kvm", O_RDWR|O_CLOEXEC);
    };
    virtual ~kvm() {
        close(dev_fd);
    }
    // copy constructor
    kvm(const kvm&) = delete;

    // copy assignment
    kvm& operator=(const kvm&) = delete;

    // move constructor
    kvm(kvm&& other) noexcept : dev_fd(other.dev_fd) {
        other.dev_fd = -1;
    };

    // move assignment
    kvm& operator=(kvm&& other) noexcept {
        if (this != &other) {
            if (dev_fd >= 0) close(dev_fd);
            dev_fd = other.dev_fd;
            
            // invalidate source
            other.dev_fd = -1;
        }
        
        return *this;
    };
   
    std::unique_ptr<microVM> create_vm(const std::vector<vcpu>& vcpus, size_t ram_size) {
        int vm_fd = ioctl(dev_fd, KVM_CREATE_VM, 0);
        // set up memory
        struct kvm_userspace_memory_region mem_region;
        mem_region.slot = 0;
        mem_region.flags = 0;
        mem_region.guest_phys_addr = 0;
        mem_region.memory_size = ram_size;
        mem_region.userspace_addr = (unsigned long)vm->mem;
        if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &mem_region) < 0) {
            std::cerr << "Error setting user memory region\n";
        };

        int vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
        int mmap_size = ioctl(dev_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
        struct kvm_run* run = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, vcpu_fd, 0);
        if (run == MAP_FAILED) {
            std::cerr << "Error mapping memory for vcpu\n";
        }

        // put assembled code on user memory region

        // set up vcpu registers

        // run and handle exit reason
    }

    int get_vcpu_mmap_size() {
        return ioctl(sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0); 
    }
    
    int dev_fd; // the KVM device
    int sys_fd; // the system device (used for system ioctl calls)
};

#endif