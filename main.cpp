#define KVM_FD "/dev/kvm"
#define RAM_SIZE 1024 * 1024 * 1024

struct kvm {
    int kvm_fd;
    int ram_fd;
    int ram_size;
    int vcpu_count;
    struct vcpu *vcpus;
};

struct vcpu {
    int vcpu_fd;
    int vcpu_id;
};

int main() {

    return 0;
}