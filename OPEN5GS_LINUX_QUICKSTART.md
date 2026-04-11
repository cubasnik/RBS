# Open5GS + RBS Quickstart (Ubuntu, SCTP)

This guide runs a minimal S1 Setup Request/Response interop check between RBS (eNB side) and Open5GS MME over native SCTP.

## 1) Install Open5GS on Ubuntu

```bash
sudo apt update
sudo apt install -y open5gs
```

Optional tools for diagnostics:

```bash
sudo apt install -y iproute2 net-tools tcpdump
```

## 2) Configure Open5GS MME S1AP bind address

Edit MME config:

```bash
sudo nano /etc/open5gs/mme.yaml
```

Set S1AP server address (example uses 10.10.10.10):

```yaml
mme:
  s1ap:
    server:
      - address: 10.10.10.10
```

Restart and check status:

```bash
sudo systemctl restart open5gs-mmed
sudo systemctl status open5gs-mmed --no-pager
```

Verify SCTP listener on 36412:

```bash
sudo ss -lnp | grep 36412
```

Expected: a listening socket for `open5gs-mmed` on SCTP `:36412`.

## 3) Configure RBS to point at MME

In `rbs.conf` set:

```ini
[lte]
mme_addr = 10.10.10.10
mme_port = 36412
```

## 4) Build and run RBS (LTE only)

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/rbs_node rbs.conf lte
```

### Windows

usrsctp fetched automatically; no extra install needed.

```powershell
cmake -S . -B build
cmake --build build --config Debug -j
.\build\Debug\rbs_node.exe rbs.conf lte
```

Expected startup log on Windows:

```text
[INFO ] [SctpSocket] [S1AP-<enb-id>] usrsctp SCTP association established
[INFO ] [S1AP]       [<enb-id>] S1AP-соединение с MME 10.10.10.10:36412 установлено (local port <n>)
```

## 5) Verify S1 Setup Request/Response

In RBS logs, confirm:

- S1AP connect to MME
- S1 Setup Request transmit
- S1 Setup Response decode line:

```text
[INFO ] [S1AP] [<enb-id>] RX S1SetupResponse decoded from <mme-ip>:36412 (len=<n>)
```

On Open5GS side, check MME logs:

```bash
sudo journalctl -u open5gs-mmed -f
```

You should see incoming S1AP activity from the eNB peer.

## 6) Common issues

- No response from MME:
  - Check firewall on both hosts (SCTP/36412 allowed).
  - Confirm `mme_addr` in `rbs.conf` matches MME bind address.
  - Confirm route between hosts.

- Running RBS on Windows:
  - RBS uses **usrsctp** (userspace SCTP, fetched automatically by CMake) on Windows — no kernel driver needed.
  - The build and run steps are identical; `rbs_node` will print `usrsctp SCTP association established` instead of a native connect message.
  - Open5GS MME must still run on Linux (Open5GS has no Windows port). Use a Linux VM or remote host for the MME side.

- Verify packet path (optional):

```bash
sudo tcpdump -ni any sctp port 36412
```
