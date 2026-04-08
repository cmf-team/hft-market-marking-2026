FROM ubuntu:24.04

ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        clang \
        clang-tidy \
        clang-format \
        gdb \
        gdbserver \
        git \
        python3 \
        pkg-config \
        libgtest-dev \
        ca-certificates \
        sudo \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04 ships with a default `ubuntu` user at UID 1000. Re-target it
# to whatever the host user's UID/GID are so bind-mounted files don't end up
# owned by root or by an unrelated user.
RUN if id ubuntu >/dev/null 2>&1; then userdel -r ubuntu || true; fi \
    && if ! getent group ${USER_GID} >/dev/null; then groupadd --gid ${USER_GID} dev; fi \
    && if ! id -u ${USER_UID} >/dev/null 2>&1; then \
           useradd --uid ${USER_UID} --gid ${USER_GID} --create-home --shell /bin/bash dev; \
       fi \
    && echo "dev ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/dev \
    && chmod 0440 /etc/sudoers.d/dev

USER dev
WORKDIR /workspace

CMD ["bash"]
