FROM mcr.microsoft.com/devcontainers/python:3.12-bookworm

# Build-only system dependencies for PlatformIO (espressif32 / Arduino).
RUN export DEBIAN_FRONTEND=noninteractive \
  && apt-get update \
  && apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv \
    git curl ca-certificates unzip tar xz-utils \
    build-essential cmake ninja-build pkg-config ccache \
  && rm -rf /var/lib/apt/lists/* \
  && python3 --version && pip3 --version && git --version \
  && cmake --version && ninja --version && pkg-config --version

USER vscode
RUN curl -fsSL https://opencode.ai/v2/install | bash
USER root
# Pre-create bind-mount targets so Docker doesn't auto-create them as root.
RUN vscode_home="$(getent passwd vscode | cut -d: -f6)" \
  && mkdir -p "$vscode_home/.config/opencode" "$vscode_home/.local/share/opencode" \
  && chown -R vscode:vscode "$vscode_home/.config" "$vscode_home/.local"
