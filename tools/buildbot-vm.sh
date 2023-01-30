#! /bin/bash
#
# Copyright (C) 2023 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

ART_TEST_ON_VM=true . "$(dirname $0)/buildbot-utils.sh"

known_actions="create|boot|setup-ssh|connect|quit"

if [[ -z $ANDROID_BUILD_TOP ]]; then
    msgfatal "ANDROID_BUILD_TOP is not set"
elif [[ ( $# -ne 1 ) || ! ( "$1" =~ ^($known_actions)$ ) ]]; then
    msgfatal "usage: $0 <$known_actions>"
fi

action="$1"

get_stable_binary() {
    mkdir tmp && cd tmp
    wget "http://security.ubuntu.com/ubuntu/pool/main/$1"
    7z x "$(basename $1)" && zstd -d data.tar.zst && tar -xf data.tar
    mv "$2" ..
    cd .. && rm -rf tmp
}

if [[ $action = create ]]; then
(
    rm -rf "$ART_TEST_VM_DIR"
    mkdir -p "$ART_TEST_VM_DIR"
    cd "$ART_TEST_VM_DIR"

    # sudo apt install qemu-system-<arch> qemu-efi cloud-image-utils

    # Get the cloud image for Ubunty 22.04 (Jammy)
    wget "http://cloud-images.ubuntu.com/releases/22.04/release/$ART_TEST_VM_IMG"

    if [[ "$TARGET_ARCH" = "riscv64" ]]; then
        # Get U-Boot for Ubuntu 22.04 (Jammy)
        get_stable_binary \
            u/u-boot/u-boot-qemu_2022.01+dfsg-2ubuntu2.3_all.deb \
            usr/lib/u-boot/qemu-riscv64_smode/uboot.elf

        # Get OpenSBI for Ubuntu 22.04 (Jammy)
        get_stable_binary \
            o/opensbi/opensbi_1.1-0ubuntu0.22.04.1_all.deb \
            usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.elf

    elif [[ "$TARGET_ARCH" = "arm64" ]]; then
        # Get EFI (ARM64) for Ubuntu 22.04 (Jammy)
        get_stable_binary \
            e/edk2/qemu-efi-aarch64_2022.02-3ubuntu0.22.04.1_all.deb \
            usr/share/qemu-efi-aarch64/QEMU_EFI.fd

        dd if=/dev/zero of=flash0.img bs=1M count=64
        dd if=QEMU_EFI.fd of=flash0.img conv=notrunc
        dd if=/dev/zero of=flash1.img bs=1M count=64
    fi

    qemu-img resize "$ART_TEST_VM_IMG" +128G

    # https://help.ubuntu.com/community/CloudInit
    cat >user-data <<EOF
#cloud-config
ssh_pwauth: true
chpasswd:
  expire: false
  list:
    - $ART_TEST_SSH_USER:ubuntu
EOF
    cloud-localds user-data.img user-data
)
elif [[ $action = boot ]]; then
(
    cd "$ART_TEST_VM_DIR"
    if [[ "$TARGET_ARCH" = "riscv64" ]]; then
        qemu-system-riscv64 \
            -m 16G \
            -smp 8 \
            -M virt \
            -nographic \
            -bios fw_jump.elf \
            -kernel uboot.elf \
            -drive file="$ART_TEST_VM_IMG",if=virtio \
            -drive file=user-data.img,format=raw,if=virtio \
            -device virtio-net-device,netdev=usernet \
            -netdev user,id=usernet,hostfwd=tcp::$ART_TEST_SSH_PORT-:22
    elif [[ "$TARGET_ARCH" = "arm64" ]]; then
        qemu-system-aarch64 \
            -m 16G \
            -smp 8 \
            -cpu cortex-a57 \
            -M virt \
            -nographic \
            -drive if=none,file="$ART_TEST_VM_IMG",id=hd0 \
            -pflash flash0.img \
            -pflash flash1.img \
            -drive file=user-data.img,format=raw,id=cloud \
            -device virtio-blk-device,drive=hd0 \
            -device virtio-net-device,netdev=usernet \
            -netdev user,id=usernet,hostfwd=tcp::$ART_TEST_SSH_PORT-:22
    fi

)
elif [[ $action = setup-ssh ]]; then
    # Clean up mentions of this VM from known_hosts
    sed -i -E "/\[$ART_TEST_SSH_HOST.*\]:$ART_TEST_SSH_PORT .*/d" $HOME/.ssh/known_hosts
    ssh-copy-id -p "$ART_TEST_SSH_PORT" "$ART_TEST_SSH_USER@$ART_TEST_SSH_HOST"

elif [[ $action = connect ]]; then
    ssh -p "$ART_TEST_SSH_PORT" "$ART_TEST_SSH_USER@$ART_TEST_SSH_HOST"

elif [[ $action = quit ]]; then
    ssh -p "$ART_TEST_SSH_PORT" "$ART_TEST_SSH_USER@$ART_TEST_SSH_HOST" "sudo poweroff"

fi
