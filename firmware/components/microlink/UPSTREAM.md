Vendored from https://github.com/Csontikka/microlink (MIT, Cameron Malone), MicroLink v2.
Unofficial Tailscale-compatible client: ts2021 control protocol, WireGuard (wireguard-lwip),
DERP relay, DISCO/STUN. The WireGuard netif claims 100.64.0.0/10 in lwIP, so ordinary BSD
sockets (libssh) reach tailnet peers directly.
Local changes: none besides moving wireguard_lwip to a sibling component directory.
- ml_wg_mgr.c: config.enable_disco=false now really disables direct-path upgrades (relay-only mode).
- ml_coord.c: advertise the netcheck-chosen home DERP as PreferredDERP (was always the compile-time default).
- wireguardif.h: WIREGUARDIF_MTU 1420 -> 1280 (Tailscale tunnel MTU).
