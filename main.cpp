#include <cstdint>
#define KVM_FD "/dev/kvm"
#define MEM_SIZE 0x40000000
#define USER_ENTRY 0x0
// #define VCPU_COUNT 1

#include "kvm.cpp"
#include <span>

int main() {
    uint8_t instructions[] = "\xB0\x61\xBA\x17\x02\xEE\xB0\n\xEE\xF4";
    kvm kvm;
    microVM vm = kvm.create_vm(MEM_SIZE, USER_ENTRY, instructions);
    vm.run(instructions);
    // vm.stop();
    // vm.destroy();
    return 0;
}