/*
Relevant system calls:
ioctl(vm->sys_fd, KVM_CREATE_VM, 0)
ioctl(vm->fd, KVM_CREATE_VCPU, 0);
ioctl(vm->sys_fd, KVM_GET_VCPU_MMAP_SIZE, 0)
ioctl(vm->sys_fd, KVM_GET_API_VERSION, 0)
ioctl(vm->fd, KVM_SET_TSS_ADDR, 0xfffbd000)
ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &memreg) 
ioctl(vcpu->fd, KVM_RUN, 0)
ioctl(vcpu->fd, KVM_GET_REGS, &regs)
ioctl(vcpu->fd, KVM_SET_REGS, &regs)
ioctl(vcpu->fd, KVM_GET_SREGS, &sregs)
ioctl(vcpu->fd, KVM_SET_SREGS, &sregs)

*/

#ifndef KVM_HPP
#define KVM_HPP

#include <vector>

#define KVM_FD "/dev/kvm"
// #define RAM_SIZE 512000000
// #define VCPU_COUNT 1
struct vcpu {
    public:
        vcpu(int vcpu_id);
        ~vcpu();
        int vcpu_run();
        int vcpu_stop();
        int vcpu_destroy();
    private:
        int vcpu_fd;
        int vcpu_id;
};

struct microVM {
    public:
        microVM(int vm_fd);
        virtual ~microVM();
        int create_vcpu();
        int destroy_vcpu();
    private:
        int vm_fd; // the VM instance
};

struct kvm {
    public:
        kvm();
        virtual ~kvm();
        static kvm& get_instance(); // Singleton pattern
        std::unique_ptr<microVM> create_vm(const std::vector<vcpu>& vcpus, const int32_t ram_size);
    private:
        int dev_fd; // the KVM device
};

#endif