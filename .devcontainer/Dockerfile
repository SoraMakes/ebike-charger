FROM mcr.microsoft.com/devcontainers/python:3.12-bookworm

COPY install-platformio-deps.sh /tmp/install-platformio-deps.sh
RUN bash /tmp/install-platformio-deps.sh && rm /tmp/install-platformio-deps.sh

USER vscode
RUN curl -fsSL https://opencode.ai/v2/install | bash
USER root
# Pre-create bind-mount targets so Docker doesn't auto-create them as root.
RUN vscode_home="$(getent passwd vscode | cut -d: -f6)" \
  && mkdir -p "$vscode_home/.config/opencode" "$vscode_home/.local/share/opencode" \
  && chown -R vscode:vscode "$vscode_home/.config" "$vscode_home/.local"
