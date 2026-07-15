from __future__ import annotations

import os
import sys

import paramiko


HOST = os.environ.get("REMOTE_HOST", "185.219.41.64")
USER = os.environ.get("REMOTE_USER", "root")


def main() -> int:
    password = os.environ.get("DEPLOY_SSH_PASSWORD")
    if not password:
        print("DEPLOY_SSH_PASSWORD is required", file=sys.stderr)
        return 2

    if len(sys.argv) < 2:
        print("Usage: remote_exec.py <command>", file=sys.stderr)
        return 2

    command = " ".join(sys.argv[1:])

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        HOST,
        username=USER,
        password=password,
        timeout=30,
        banner_timeout=30,
        auth_timeout=30,
    )
    stdin, stdout, stderr = client.exec_command(command)
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    status = stdout.channel.recv_exit_status()
    client.close()

    if out:
        sys.stdout.write(out)
    if err:
        sys.stderr.write(err)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
