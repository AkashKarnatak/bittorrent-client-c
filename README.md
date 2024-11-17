# BitTorrent Client

Simple BitTorrent client from scratch in C

# Running

### Print torrent info

```sh
./your_bittorrent.sh info sample.torrent
```

### Discover peers

```sh
./your_bittorrent.sh peers sample.torrent
```

### Perform peer handshake

```sh
./your_bittorrent.sh handshake sample.torrent <peer_ip>:<peer_port>
```

### To download a piece

```sh
./your_bittorrent.sh download_piece -o /tmp/test-piece sample.torrent <piece_index>
```

### To download a file

```sh
./your_bittorrent.sh download -o /tmp/test.txt sample.torrent
```
