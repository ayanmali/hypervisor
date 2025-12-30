#define KVM_FD "/dev/kvm"
#define RAM_SIZE 512000000
#define VCPU_COUNT 1

#include "kvm.hpp"



int main() {
    kvm kvm;
    microVM vm(kvm.create_vm(VCPU_COUNT, RAM_SIZE));
    vm.run();
    vm.stop();
    vm.destroy();
    return 0;
}