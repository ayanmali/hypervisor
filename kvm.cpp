/*
Relevant system calls:

queries extensions to the core KVM API. User passes an extension identifier (an
int) and receives an int (0 or 1) describing the extension availability
iotctl(vm->fd, KVM_CHECK_EXTENSION, KVM_CAP_*)

Shows the maximum allowed number of memory slots.
KVM_CAP_NR_MEMSLOTS


ioctl(vm->sys_fd, KVM_CREATE_VM, 0)
ioctl(vm->fd, KVM_CREATE_VCPU, 0);
ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0)
ioctl(vm->sys_fd, KVM_GET_API_VERSION, 0)

defines physical address of a three-page region in the guest physical address
space. Region must be within the first 4 GB of the guest physical address space
and must not conflict with any existing regions or mmio addresses. ioctl(vm->fd,
KVM_SET_TSS_ADDR, 0xfffbd000)


Allows user to create/modify/delete a guest physical memory slot.
Bits 0-15 of "slot" specify the slot id; should be less than max number of user
memory slots supported per VM. if KVM_CAP_MULTI_ADDRESS_SPACE is supported, bits
16-31 specify the address space id. ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION,
&struct kvm_userspace_memory_region)

Runs a guest vcpu
mmap()ing a vcpu fd at offset 0  with the size given by KVM_GET_VCPU_MMAP_SIZE
returns a pointer to a struct kvm_run. ioctl(vcpu->fd, KVM_RUN, 0)

ioctl(vcpu->fd, KVM_GET_REGS, &regs)
ioctl(vcpu->fd, KVM_SET_REGS, &regs)
ioctl(vcpu->fd, KVM_GET_SREGS, &sregs)
ioctl(vcpu->fd, KVM_SET_SREGS, &sregs)

ioctl(vcpu->fd, KVM_SET_CPUID2, &struct kvm_cpuid2)

sets the physical address of a one-page region in the guest physical address
space. Setting address to 0 resets the address to its default (0xfffbc000).
Required on Intel. Fails if KVM_CAP_SET_IDENTITY_MAP_ADDR is not supported or if
any vcpu has already been created. ioctl(vm->fd, KVM_SET_IDENTITY_MAP_ADDR, 0)

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
a  typical configuration would have CS with base=0, limit=0xFFFFFFFF, type=0xb
(execute/read), L=1 (64-bit), and DPL=0 (kernel mode) or DPL=3 (user mode)
*/

#ifndef KVM_HPP
#define KVM_HPP

#include <cstdint>
#include <cstring>
#include <iostream>
#include <linux/kvm.h>
#include <span>
#include <unistd.h>
#include <vector>

#define KVM_FD "/dev/kvm"
#define TSS_ADDR 0xfffbd000 // the TSS address (Intel x86)
// #define RAM_SIZE 512000000
// #define VCPU_COUNT 1

/*
ioctl(vm->fd, KVM_CREATE_VCPU, 0);
ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0)
*/
struct vcpu_t {
  int fd;
  int id;
  struct kvm_run *run; // memory available for this vCPU
  struct kvm_regs *regs;
  struct kvm_sregs *sregs;

  /*
  mmap()ing a vpcu fd returns a pointer to a struct kvm_run.
  */
  vcpu_t(size_t mmap_size, int vcpu_fd, int vcpu_id)
      : fd(vcpu_fd), id(vcpu_id) {
    run = (struct kvm_run *)mmap(0, mmap_size, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, vcpu_fd, 0);
  };
  virtual ~vcpu_t() { close(fd); };

  // KVM_GET_REGS system call
  // Returns the registers + the int status code
  std::pair<struct kvm_regs, int> get_regs() {
    struct kvm_regs regs;
    int status = ioctl(fd, KVM_GET_REGS, &regs);
    return std::pair<struct kvm_regs, int>{regs, status};
  };
  // KVM_GET_SREGS system call
  std::pair<struct kvm_sregs, int> get_sregs() {
    struct kvm_sregs sregs;
    int status = ioctl(fd, KVM_GET_SREGS, &sregs);
    return std::pair<struct kvm_sregs, int>{sregs, status};
  };
  // KVM_SET_REGS system call
  int set_regs(struct kvm_regs *regs) {
    int status = ioctl(fd, KVM_SET_REGS, regs);
    return status;
  };
  // KVM_SET_SREGS system call
  int set_sregs(struct kvm_sregs *sregs) {
    int status = ioctl(fd, KVM_SET_SREGS, sregs);
    return status;
  }
  // KVM_SET_CPUID system call
  // TODO: fallback to KVM_SET_CPUID if not available on this system
  int set_vcpu_cpuid(struct kvm_cpuid *cpuid) {
    return ioctl(fd, KVM_SET_CPUID2, cpuid);
  }
  // KVM_GET_CPUID2 system call
  int get_vcpu_cpuid() { return ioctl(fd, KVM_GET_CPUID2); };
};

// a VM instance created using KVM
struct microVM {
  int fd;                    // the VM instance
  size_t mmap_size;          // size of memory available to the VM
  void* userspace_address;
  size_t user_entry;
  std::vector<vcpu_t> vcpus;

  explicit microVM(int dev_fd) {
    fd = ioctl(dev_fd, KVM_CREATE_VM, 0);
    mmap_size = ioctl(dev_fd, KVM_GET_VCPU_MMAP_SIZE, NULL);
  }
  virtual ~microVM() {
    // destroy each vCPU first
    for (const auto &vcpu : vcpus) {
      vcpu.~vcpu_t();
    }
    close(fd);
  }
  int run(std::span<uint8_t> instructions) {
    if (vcpus.size() < 1)
      std::cerr << "Error: no vCPUs present in this VM.\n";
    // copy instructions into memory
    std::memcpy((void *)((size_t)userspace_address + user_entry), instructions.data(),
                instructions.size());
    while (1) {
      int status = ioctl(vcpus[0].fd, KVM_RUN, NULL);
      if (status < 0) std::cerr << "Error running VM\n";
      switch (run->exit_reason) {
        case KVM_EXIT_HLT:
            fputs("KVM_EXIT_HLT", stderr);
            return 0;
        case KVM_EXIT_IO:
            /* TODO: check port and direction here */
            putchar(*(((char *)run) + run->io.data_offset));
            break;
        case KVM_EXIT_FAIL_ENTRY:
            errx(1, "KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx",
                run->fail_entry.hardware_entry_failure_reason);
        case KVM_EXIT_INTERNAL_ERROR:
            errx(1, "KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x",
                run->internal.suberror);
        case KVM_EXIT_SHUTDOWN:
            errx(1, "KVM_EXIT_SHUTDOWN");
        default:
            errx(1, "Unhandled reason: %d", run->exit_reason);
      }
      return 0;
    }
  }
  int stop();
  int destroy();

  // id is an int in the range [0, max_id] where max_id is obtained from
  vcpu_t add_vcpu(size_t mmap_size, int vcpu_id) {
    int vcpu_fd = ioctl(fd, KVM_CREATE_VCPU, 0);
    vcpu_t vcpu = vcpu_t(mmap_size, vcpu_fd, vcpu_id);
    vcpus.push_back(vcpu);
    return vcpu;
  }

  struct kvm_userspace_memory_region allocate_memory(size_t mem_size) {
    void *mem = (void *)mmap(0, mem_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    struct kvm_userspace_memory_region mem_region = {.slot = 0,
                                                     .flags = 0,
                                                     .guest_phys_addr = 0,
                                                     .memory_size = mem_size,
                                                     .userspace_addr =
                                                         (size_t)mem};
    userspace_address = mem;
    
    return mem_region;
  }
  int remove_vcpu(vcpu_t &vcpu);
  vcpu_t get_vcpu_by_id(int vcpu_id);
  // gets recommended max vcpu using KVM_CAP_NR_VCPUS of KVM_CHECK_EXTENSION
  // ioctl
  int get_max_vcpu_count() {
    int count = ioctl(fd, KVM_CHECK_EXTENSION, KVM_CAP_NR_VCPUS);
    return count;
  }
};

struct kvm {
  int dev_fd; // the KVM device
  int sys_fd; // the system device (used for system ioctl calls)

  kvm() {
    dev_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    assert(dev_fd > -1);
  };
  virtual ~kvm() { close(dev_fd); }
  // copy constructor
  kvm(const kvm &) = delete;

  // copy assignment
  kvm &operator=(const kvm &) = delete;

  // move constructor
  kvm(kvm &&other) noexcept : dev_fd(other.dev_fd) { other.dev_fd = -1; };

  // move assignment
  kvm &operator=(kvm &&other) noexcept {
    if (this != &other) {
      if (dev_fd >= 0)
        close(dev_fd);
      dev_fd = other.dev_fd;

      // invalidate source
      other.dev_fd = -1;
    }

    return *this;
  };

  microVM create_vm(size_t mem_size, size_t entry,
                    std::span<uint8_t> instructions) {
    microVM vm = microVM(dev_fd);
    if (vm.fd < 0) {
      std::cerr << "Error creating virtual machine\n";
    }

    vm.user_entry = entry;
    struct kvm_userspace_memory_region mem_region =
        vm.allocate_memory(mem_size);

    if (ioctl(vm.fd, KVM_SET_USER_MEMORY_REGION, &mem_region) < 0) {
      std::cerr << "Error setting user memory region\n";
    };

    // TODO: Support multiple vCPUs?
    // vcpu_t vcpu;
    // for (int i = 0; i < num_vcpus; ++i) {
    //     vcpu = vm.add_vcpu(i);
    //     if (vcpu.fd < 0) {
    //         std::cerr << "Error creating vCPU " << i << " of " << num_vcpus
    //         << "\n";
    //     }
    // }

    vcpu_t vcpu = vm.add_vcpu(0);
    if (vcpu.run == MAP_FAILED) {
      std::cerr << "Error mapping memory for vcpu\n";
    }

    // set up vcpu registers
    struct kvm_regs regs;
    auto [regs, status] = vcpu.get_regs();
    if (status < 0)
      std::cerr << "Error getting vCPU registers\n";

    regs.rip = entry;
    regs.rsp = 0x200000; // stack address
    regs.rflags = 0x2;   // must be 1 on x86
    status = vcpu.set_regs(&regs);
    if (status < 0)
      std::cerr << "Error setting vCPU registers\n";

    // special registers
    struct kvm_sregs sregs;
    auto [sregs, status] = vcpu.get_sregs();
    if (status < 0)
      std::cerr << "Error getting vCPU special registers\n";
    sregs.cs.base = sregs.cs.selector = 0; // set base of code segment to 0
    status = vcpu.set_sregs(&sregs);
    if (status < 0)
      std::cerr << "Error setting vCPU special registers\n";

    return vm;
  }
};

#endif