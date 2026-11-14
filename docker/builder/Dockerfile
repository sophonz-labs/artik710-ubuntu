# Native arm64 build environment for the ARTIK710 kernel, device tree and rootfs.
FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential bc bison flex libssl-dev libelf-dev libncurses-dev \
      device-tree-compiler u-boot-tools kmod cpio rsync git ca-certificates \
      python3 python3-dev swig libgnutls28-dev uuid-dev \
      xz-utils zstd lz4 gzip bzip2 file wget curl busybox-static \
      mmdebstrap debootstrap e2fsprogs dosfstools fdisk parted \
      fakeroot dwarves ccache shellcheck \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
