#!/usr/bin/env python3
"""Engine addition (faf-neural-ai): a two-player lockstep game between a Windows game and the Linux
client in Docker (tools/linux-build/run.sh), the test of docs/LINUX_CLIENT.md's "Online play".

Run on the Windows host. It is the FAF client of both games, on GPGNet (one TCP port per player:
GameState Idle -> CreateLobby in autolobby mode, both in Lobby -> HostGame / JoinGame /
ConnectToPeer), and the UDP path between them: the Docker Desktop network rewrites the container's
source address, and a FAF lobby only takes peers at the address GPGNet named, so the game traffic
goes through a relay with one socket per side (as FAF's ICE adapter gives each game a local socket
per peer):
  Windows game  <->  127.0.0.1:<relay-win>   relay   127.0.0.1:<relay-linux>  <->  Linux game
The Windows game is told its peer is at 127.0.0.1:<relay-win>; the Linux game is told its peer is at
<docker-host-ip>:<relay-linux> (host.docker.internal as the container resolves it), and the relay
answers it at whatever address its datagrams arrive from (the Docker proxy's).

  python mixed-lockstep.py --host win --map SCMP_009 --seconds 900 --out run/

Then start the two games with the printed switches. Every GPGNet message is printed and written to
<out>/gpgnet.log; the relay's packet counts are printed every 10 s. The 10/11/2026 runs used, with
init_mp.lua being FAF's init_faf.lua plus MountDirectory(<tools/scenarios/mpemu-ai-armies>, "/")
(four AI armies join at launch; on Windows its first line also sets InitFileDir to FAF's bin folder):
Mounting tools/scenarios/mpemu-busy-players the same way as well makes both players' games send
orders all game long (reinforcements and attack-moves picked at random in each game's UI).

  main_x64.exe /init init_mp.lua /nobugreport /gpgnet 127.0.0.1:47101 /log win.log
      /windowed 1024 768 /players 2 /team 1 /startspot 1 /nosound /prefs mixedlockstep.prefs
      /gameoptions Victory:sandbox /checksumlog win.ck /uef
  FAF_LINUX_DOCKER_ARGS="--name faf-mp-linux" tools/linux-build/run.sh /bin/bash -c
      'bash /run-dir/gui.sh 1400 /init /fafinit/bin/init_mp.lua /nobugreport
       /gpgnet 192.168.65.254:47102 /log /run-dir/mp/linux.log /windowed 1024 768 /players 2
       /team 1 /startspot 2 /nosound /gameoptions Victory:sandbox /checksumlog /run-dir/mp/linux.ck
       /cybran'
(gui.sh: Xvfb and lavapipe around /bin-linux/faf). The two checksum logs must match line for line
(the Windows one has CRLF line ends).
"""

import argparse
import os
import socket
import struct
import sys
import threading
import time

KIND_INT, KIND_STRING, KIND_DATA = 0, 1, 2

LOG_LOCK = threading.Lock()
LOG_FILE = None


def log(text):
    line = time.strftime("%H:%M:%S ") + text
    with LOG_LOCK:
        print(line, flush=True)
        if LOG_FILE is not None:
            LOG_FILE.write(line + "\n")
            LOG_FILE.flush()


def recv_exact(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


def read_string(conn):
    (n,) = struct.unpack("<i", recv_exact(conn, 4))
    return recv_exact(conn, n)


def read_message(conn):
    command = read_string(conn).decode("utf-8", "replace")
    (count,) = struct.unpack("<i", recv_exact(conn, 4))
    args = []
    for _ in range(count):
        kind = recv_exact(conn, 1)[0]
        if kind == KIND_INT:
            args.append(struct.unpack("<i", recv_exact(conn, 4))[0])
        elif kind == KIND_STRING:
            args.append(read_string(conn).decode("utf-8", "replace"))
        else:
            args.append(read_string(conn))
    return command, args


def encode(command, *args):
    def put_string(b):
        return struct.pack("<i", len(b)) + b

    out = put_string(command.encode()) + struct.pack("<i", len(args))
    for a in args:
        if isinstance(a, int):
            out += bytes([KIND_INT]) + struct.pack("<i", a)
        else:
            out += bytes([KIND_STRING]) + put_string(str(a).encode())
    return out


class Player:
    def __init__(self, name, uid, gpg_port, lobby_port, peer_address):
        self.name, self.uid, self.gpg_port, self.lobby_port = name, uid, gpg_port, lobby_port
        self.peer_address = peer_address  # where this game reaches the other one
        self.conn = None
        self.state = None
        self.lock = threading.Lock()

    def send(self, command, *args):
        log(f"-> {self.name}: {command} {list(args)}")
        with self.lock:
            self.conn.sendall(encode(command, *args))


def serve(player, on_message):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", player.gpg_port))
    listener.listen(1)
    conn, address = listener.accept()
    listener.close()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    player.conn = conn
    log(f"{player.name}: GPGNet connected from {address}")
    try:
        while True:
            command, args = read_message(conn)
            shown = [a if not isinstance(a, bytes) else f"<{len(a)} bytes>" for a in args]
            if command == "GameState" and args:
                player.state = args[0]
            log(f"<- {player.name}: {command} {shown}")
            on_message(player, command, args)
    except (EOFError, ConnectionError, OSError) as error:
        log(f"{player.name}: GPGNet closed ({error!r})")
        player.state = "Closed"


class Relay:
    """127.0.0.1:<win_port> for the Windows game, 127.0.0.1:<linux_port> for the Linux one."""

    def __init__(self, win_port, linux_port, win_game_port):
        self.win = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.win.bind(("127.0.0.1", win_port))
        self.linux = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.linux.bind(("127.0.0.1", linux_port))  # Docker Desktop delivers host.docker.internal here
        self.win_game = ("127.0.0.1", win_game_port)
        self.linux_game = None  # latched from its first datagram
        self.counts = {"win->linux": 0, "linux->win": 0, "unlatched": 0, "relatched": 0}

    def run(self):
        threading.Thread(target=self.pump_from_linux, daemon=True).start()
        threading.Thread(target=self.pump_from_win, daemon=True).start()

    def pump_from_linux(self):
        while True:
            data, source = self.linux.recvfrom(65536)
            if self.linux_game != source:
                if self.linux_game is not None:
                    self.counts["relatched"] += 1
                log(f"relay: the Linux game is at {source}")
                self.linux_game = source
            self.win.sendto(data, self.win_game)
            self.counts["linux->win"] += 1

    def pump_from_win(self):
        while True:
            data, source = self.win.recvfrom(65536)
            if source != self.win_game:
                continue
            if self.linux_game is None:
                self.counts["unlatched"] += 1
                continue
            self.linux.sendto(data, self.linux_game)
            self.counts["win->linux"] += 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", choices=["win", "linux"], default="win", help="which game hosts")
    parser.add_argument("--map", default="SCMP_009")
    parser.add_argument("--seconds", type=int, default=900, help="stop after this long")
    parser.add_argument("--out", default=".", help="folder for gpgnet.log")
    parser.add_argument("--docker-host-ip", default="192.168.65.254",
                        help="the Windows host as the container sees it (host.docker.internal)")
    parser.add_argument("--gpg-win", type=int, default=47101)
    parser.add_argument("--gpg-linux", type=int, default=47102)
    parser.add_argument("--lobby-win", type=int, default=46001)
    parser.add_argument("--lobby-linux", type=int, default=46002)
    parser.add_argument("--relay-win", type=int, default=46101)
    parser.add_argument("--relay-linux", type=int, default=46102)
    options = parser.parse_args()

    global LOG_FILE
    os.makedirs(options.out, exist_ok=True)
    LOG_FILE = open(os.path.join(options.out, "gpgnet.log"), "w", encoding="utf-8")

    win = Player("Windows", 1 if options.host == "win" else 2, options.gpg_win, options.lobby_win,
                 f"127.0.0.1:{options.relay_win}")
    linux = Player("Linux", 2 if options.host == "win" else 1, options.gpg_linux, options.lobby_linux,
                   f"{options.docker_host_ip}:{options.relay_linux}")
    host, joiner = (win, linux) if options.host == "win" else (linux, win)
    relay = Relay(options.relay_win, options.relay_linux, options.lobby_win)
    relay.run()

    hosted = threading.Event()

    def on_message(player, command, args):
        if command == "GameState" and args and args[0] == "Idle":
            player.send("CreateLobby", 1, player.lobby_port, player.name, player.uid, 1)
        if command == "GameState" and args and args[0] == "Lobby" and not hosted.is_set():
            if host.state == "Lobby" and joiner.state == "Lobby":
                hosted.set()
                threading.Thread(target=match, daemon=True).start()

    def match():
        host.send("HostGame", options.map)
        time.sleep(2.0)
        joiner.send("JoinGame", joiner.peer_address, host.name, host.uid)
        host.send("ConnectToPeer", host.peer_address, joiner.name, joiner.uid)

    for player in (win, linux):
        threading.Thread(target=serve, args=(player, on_message), daemon=True).start()

    log(f"GPGNet: Windows game on 127.0.0.1:{options.gpg_win} (uid {win.uid}, lobby port {options.lobby_win}), "
        f"Linux game on {options.docker_host_ip}:{options.gpg_linux} (uid {linux.uid}, lobby port {options.lobby_linux}); "
        f"host: {host.name}, map {options.map}")
    log(f"  Windows: /gpgnet 127.0.0.1:{options.gpg_win} /players 2 /team 1 /startspot {win.uid}")
    log(f"  Linux:   /gpgnet {options.docker_host_ip}:{options.gpg_linux} /players 2 /team 1 /startspot {linux.uid}")

    deadline = time.time() + options.seconds
    next_report = time.time() + 10
    while time.time() < deadline:
        time.sleep(0.5)
        if time.time() >= next_report:
            next_report += 10
            log(f"relay: {relay.counts}")
        if win.state == "Closed" and linux.state == "Closed":
            break
    log(f"done: relay {relay.counts}; Windows {win.state}, Linux {linux.state}")


if __name__ == "__main__":
    sys.exit(main())
