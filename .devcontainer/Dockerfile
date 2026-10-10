FROM mcr.microsoft.com/devcontainers/python:3.12-bookworm

COPY install-platformio-deps.sh /tmp/install-platformio-deps.sh
RUN bash /tmp/install-platformio-deps.sh && rm /tmp/install-platformio-deps.sh

USER vscode
RUN curl -fsSL https://opencode.ai/v2/install | bash
USER root
# Pre-create bind-mount targets so Docker doesn't auto-create
# /home/vscode/.local as root:root at container start.
RUN mkdir -p /home/vscode/.config/opencode /home/vscode/.local/share/opencode \
  && chown -R vscode:vscode /home/vscode/.config /home/vscode/.local
