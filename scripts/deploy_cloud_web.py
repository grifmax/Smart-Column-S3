from __future__ import annotations

import os
from pathlib import Path

import paramiko


ROOT = Path(__file__).resolve().parents[1]
LOCAL_WEB = ROOT / "cloud_proxy" / "web"
REMOTE_WEB = "/var/www/smart-column/cloud_proxy/web"
HOST = "185.219.41.64"
USER = "root"


def ensure_remote_dir(sftp: paramiko.SFTPClient, path: str) -> None:
    parts = path.strip("/").split("/")
    current = ""
    for part in parts:
        current += "/" + part
        try:
            sftp.stat(current)
        except FileNotFoundError:
            sftp.mkdir(current)


def deploy() -> None:
    password = os.environ.get("DEPLOY_SSH_PASSWORD")
    if not password:
        raise SystemExit("DEPLOY_SSH_PASSWORD is required")

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=password, timeout=15)

    remote_command = os.environ.get("REMOTE_COMMAND")
    if remote_command:
        stdin, stdout, stderr = client.exec_command(remote_command)
        out = stdout.read().decode("utf-8", errors="replace")
        err = stderr.read().decode("utf-8", errors="replace")
        status = stdout.channel.recv_exit_status()
        client.close()
        if out:
            print(out, end="")
        if err:
            print(err, end="")
        raise SystemExit(status)

    sftp = client.open_sftp()

    ensure_remote_dir(sftp, REMOTE_WEB)

    for path in LOCAL_WEB.rglob("*"):
        rel = path.relative_to(LOCAL_WEB)
        remote_path = f"{REMOTE_WEB}/{rel.as_posix()}"
        if path.is_dir():
            ensure_remote_dir(sftp, remote_path)
            continue
        ensure_remote_dir(sftp, str(Path(remote_path).parent).replace("\\", "/"))
        sftp.put(str(path), remote_path)
        sftp.chmod(remote_path, 0o644)

    sftp.close()
    client.close()


if __name__ == "__main__":
    deploy()
